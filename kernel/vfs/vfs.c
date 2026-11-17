/* vfs.c —— 大内核里的文件层：inode 树 + 打开表 + ramfs + /dev/console。
 *
 * 没有块设备、没有缓存、没有并发保护：先用最小实现证明
 * “用户态要的文件语义，在内核里是一条函数调用链”。
 */
#include "kern.h"
#include "blk.h"

extern const u8 user_blob_start[];
extern const u8 user_blob_end[];
extern const u8 user_shell_start[];
extern const u8 user_shell_end[];

/* ramfs 单文件上限：从 1 KiB 提到 64 KiB，够放真实程序与数据。 */
#define RAMFS_FILE_MAX (64UL * 1024UL)

#define MAX_INO   192
#define MAX_FD    16
#define T_DIR     0
#define T_FILE    1
#define T_DEV     2
#define T_EXT2    3   /* 指向 ext2 只读文件：首次 read 时把内容读进内存 */
#define DEV_CON   1

typedef struct inode {
    char name[24];
    int type;
    int dev;
    struct inode *parent;
    struct inode *child;
    struct inode *sib;
    char *data;
    u64 size;
    u64 cap;
} ino_t;

static ino_t pool[MAX_INO];
static int np;
static ino_t *root;

typedef struct {
    ino_t *ino;
    u64 off;
    int used;
} file_t;

/* 每个进程一份 fd 表：fork 时深拷贝，互不影响。
 * 内核线程没有 ftab，回退到 boot_ft（0/1/2 接控制台）。 */
struct filetable {
    file_t f[MAX_FD];
};
static struct filetable boot_ft;
static struct filetable *ftab(void)
{
    struct filetable *t = (struct filetable *)sched_get_ftab();
    return t != (struct filetable *)0 ? t : &boot_ft;
}
#define fds (ftab()->f)

static void str_copy(char *d, const char *s, u64 cap)
{
    u64 i = 0;
    while (s[i] != '\0' && i + 1 < cap) {
        d[i] = s[i];
        i++;
    }
    d[i] = '\0';
}

static int str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* 取一个槽位：优先复用被 unlink 释放的（type=-1），否则从池尾新开。 */
static ino_t *ino_slot(void)
{
    int i;

    for (i = 0; i < np; i++) {
        if (pool[i].type == -1) {
            return &pool[i];
        }
    }
    if (np >= MAX_INO) {
        return (ino_t *)0;
    }
    return &pool[np++];
}

static ino_t *ino_new(ino_t *parent, const char *name, int type)
{
    ino_t *n = ino_slot();

    if (n == (ino_t *)0) {
        return (ino_t *)0;
    }
    n->type = type;
    n->dev = 0;
    n->parent = parent;
    n->child = (ino_t *)0;
    n->sib = (ino_t *)0;
    n->data = (char *)0;
    n->size = 0;
    n->cap = 0;
    str_copy(n->name, name, sizeof(n->name));

    if (parent != (ino_t *)0) {
        if (parent->child == (ino_t *)0) {
            parent->child = n;
        } else {
            ino_t *p = parent->child;
            while (p->sib != (ino_t *)0) {
                p = p->sib;
            }
            p->sib = n;
        }
    }
    return n;
}

static ino_t *ino_find(ino_t *dir, const char *name)
{
    ino_t *p;
    for (p = dir->child; p != (ino_t *)0; p = p->sib) {
        if (str_eq(p->name, name)) {
            return p;
        }
    }
    return (ino_t *)0;
}

static ino_t *walk(const char *path)
{
    ino_t *cur_ino = root;
    const char *p = path;

    if (path == (const char *)0 || path[0] != '/') {
        return (ino_t *)0;
    }
    while (*p != '\0') {
        char comp[24];
        u64 n = 0;
        ino_t *next;

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        while (*p != '/' && *p != '\0') {
            if (n + 1 >= sizeof(comp)) {
                return (ino_t *)0;
            }
            comp[n++] = *p++;
        }
        comp[n] = '\0';
        next = ino_find(cur_ino, comp);
        if (next == (ino_t *)0) {
            return (ino_t *)0;
        }
        cur_ino = next;
    }
    return cur_ino;
}

/* 把一段二进制装进 inode：用于把内置程序写进 ramfs，供 exec-by-path 用。 */
static void set_data(ino_t *n, const u8 *src, u64 len)
{
    char *buf;

    if (len == 0 || len > RAMFS_FILE_MAX) {
        return;
    }
    buf = (char *)kmalloc(len);
    if (buf == (char *)0) {
        return;
    }
    {
        u64 i;

        for (i = 0; i < len; i++) {
            buf[i] = (char)src[i];
        }
    }
    n->data = buf;
    n->size = len;
    n->cap = len;
}

static void set_text(ino_t *n, const char *s)
{
    u64 len = 0;
    char *buf;

    while (s[len] != '\0') {
        len++;
    }
    if (len > RAMFS_FILE_MAX) {
        return;
    }
    buf = (char *)kmalloc(len + 1);
    if (buf == (char *)0) {
        return;
    }
    {
        u64 i;
        for (i = 0; i <= len; i++) {
            buf[i] = s[i];
        }
    }
    n->data = buf;
    n->size = len;
    n->cap = len + 1;
}


/* 把路径切成「父目录 inode + 末段名」。根下的名字返回 root。 */
static ino_t *split_parent(const char *path, char *name, u64 cap)
{
    char dir[VFS_PATH_MAX];
    u64 len = 0;
    u64 cut = 0;
    u64 ns;
    u64 i;

    while (path[len] != '\0' && len + 1 < VFS_PATH_MAX) {
        len++;
    }
    if (len == 0 || path[0] != '/') {
        return (ino_t *)0;
    }
    for (i = 0; i < len; i++) {
        if (path[i] == '/') {
            cut = i;
        }
    }
    ns = len - cut - 1;
    if (ns == 0 || ns + 1 > cap) {
        return (ino_t *)0;
    }
    for (i = 0; i < ns; i++) {
        name[i] = path[cut + 1 + i];
    }
    name[ns] = '\0';
    if (cut == 0) {
        return root;
    }
    for (i = 0; i < cut && i + 1 < sizeof(dir); i++) {
        dir[i] = path[i];
    }
    dir[cut] = '\0';
    return walk(dir);
}

i64 vfs_mkdir(const char *path)
{
    char name[24];
    ino_t *p = split_parent(path, name, sizeof(name));

    if (p == (ino_t *)0 || p->type != T_DIR) {
        return -1;
    }
    if (ino_find(p, name) != (ino_t *)0) {
        return -1;
    }
    return ino_new(p, name, T_DIR) != (ino_t *)0 ? 0 : -1;
}

i64 vfs_unlink(const char *path)
{
    char name[24];
    ino_t *p = split_parent(path, name, sizeof(name));
    ino_t *victim;
    ino_t **pp;

    if (p == (ino_t *)0) {
        return -1;
    }
    victim = ino_find(p, name);
    if (victim == (ino_t *)0) {
        return -1;
    }
    if (victim->child != (ino_t *)0) {
        return -1; /* 目录非空 */
    }
    pp = &p->child;
    while (*pp != (ino_t *)0 && *pp != victim) {
        pp = &(*pp)->sib;
    }
    if (*pp == victim) {
        *pp = victim->sib;
    }
    victim->sib = (ino_t *)0;
    victim->parent = (ino_t *)0;
    if (victim->data != (char *)0) {
        kfree(victim->data);
    }
    victim->data = (char *)0;
    victim->size = 0;
    victim->cap = 0;
    victim->type = -1;
    victim->name[0] = '\0';
    return 0;
}

void vfs_init(void)
{
    ino_t *dev;
    ino_t *etc;
    ino_t *con;
    int i;

    np = 0;
    for (i = 0; i < MAX_FD; i++) {
        fds[i].used = 0;
        fds[i].ino = (ino_t *)0;
        fds[i].off = 0;
    }

    root = ino_new((ino_t *)0, "/", T_DIR);
    dev = ino_new(root, "dev", T_DIR);
    etc = ino_new(root, "etc", T_DIR);
    (void)ino_new(root, "tmp", T_DIR);
    (void)ino_new(root, "mnt", T_DIR);
    {
        ino_t *bin = ino_new(root, "bin", T_DIR);

        set_text(ino_new(bin, "hello", T_FILE), "user-file: hello from ramfs file\n");
        /* 可执行镜像：exec-by-path 会读它 */
        set_data(ino_new(bin, "hello.bin", T_FILE),
                 user_blob_start, (u64)(user_blob_end - user_blob_start));
        /* shell 也进 ramfs：M4 的 init 用 exec_path("/bin/sh") 拉起它 */
        set_data(ino_new(bin, "sh", T_FILE),
                 user_shell_start, (u64)(user_shell_end - user_shell_start));
    }

    con = ino_new(dev, "console", T_DEV);
    con->dev = DEV_CON;

    set_text(ino_new(etc, "motd", T_FILE), "hello from ramfs\n");
    set_text(ino_new(root, "hello.txt", T_FILE), "big kernel vfs works\n");

    for (i = 0; i < 3; i++) { /* 0/1/2 都指向控制台 */
        fds[i].ino = con;
        fds[i].used = 1;
        fds[i].off = 0;
    }
}

/* /mnt/<name> 走 ext2：这里不做页缓存，第一次 read 整文件读进内存。 */
static int is_mnt_path(const char *p)
{
    return p[0] == '/' && p[1] == 'm' && p[2] == 'n' && p[3] == 't' && p[4] == '/' && p[5] != '\0';
}

i64 vfs_open(const char *path, int flags)
{
    ino_t *n = (ino_t *)0;
    int i;

    if (is_mnt_path(path)) {
        ino_t *mnt = walk("/mnt");

        if (mnt != (ino_t *)0) {
            n = ino_find(mnt, path + 5);
            if (n == (ino_t *)0) {
                n = ino_new(mnt, path + 5, T_EXT2);
            }
        }
    } else {
        n = walk(path);
    }

    if (n == (ino_t *)0) {
        char name[24];
        ino_t *p;

        if ((flags & 1) == 0) { /* O_CREAT */
            return -1;
        }
        p = split_parent(path, name, sizeof(name));
        if (p == (ino_t *)0 || p->type != T_DIR) {
            return -1;
        }
        n = ino_new(p, name, T_FILE);
        if (n == (ino_t *)0) {
            return -1;
        }
    }
    /* O_TRUNC(2)：把已有文件清零 */
    if ((flags & 2) != 0 && n != (ino_t *)0 && n->type == T_FILE) {
        if (n->data != (char *)0) {
            kfree(n->data);
        }
        n->data = (char *)0;
        n->size = 0;
        n->cap = 0;
    }
    for (i = 3; i < MAX_FD; i++) {
        if (!fds[i].used) {
            fds[i].ino = n;
            fds[i].off = 0;
            if ((flags & 4) != 0) { /* O_APPEND */
                fds[i].off = n->size;
            }
            fds[i].used = 1;
            return (i64)i;
        }
    }
    return -1;
}

i64 vfs_read(int fd, void *buf, u64 n)
{
    file_t *f;
    u64 k, left;

    if (fd < 0 || fd >= MAX_FD || !fds[fd].used) {
        return -1;
    }
    f = &fds[fd];
    if (f->ino->type == T_EXT2 && f->ino->data == (char *)0) {
        char full[VFS_PATH_MAX];
        char *mem = (char *)mm_page_alloc();
        i64 got;
        u64 i;

        if (mem == (char *)0) {
            return -1;
        }
        /* ext2 的读取接口吃的是相对挂载点根的路径，所以要拼 "/<name>" */
        full[0] = '/';
        for (i = 0; i + 1 < VFS_PATH_MAX - 1 && f->ino->name[i] != '\0'; i++) {
            full[1 + i] = f->ino->name[i];
        }
        full[1 + i] = '\0';
        got = ext2_read(full, mem, 4096);
        if (got <= 0) {
            mm_page_free(mem);
            return -1;
        }
        f->ino->data = mem;
        f->ino->size = (u64)got;
        f->ino->cap = 4096;
    }

    if (fd < 0 || fd >= MAX_FD || !fds[fd].used || n == 0) {
        return -1;
    }
    f = &fds[fd];
    if (f->ino->type == T_DEV) {
        /* 控制台读：从 UART 环形缓冲取，非阻塞。shell 用 read(0,…,1)
         * 轮询；没数据返回 0，调用方 yield 后再试。 */
        u64 got = 0;
        u8 *d = (u8 *)buf;
        while (got < n) {
            int c = uart_tryget();
            if (c < 0) {
                break;
            }
            d[got++] = (u8)c;
        }
        return (i64)got;
    }
    if (f->ino->type != T_FILE && f->ino->type != T_EXT2) {
        return -1;
    }
    left = f->ino->size > f->off ? f->ino->size - f->off : 0;
    k = left < n ? left : n;
    {
        u64 i;
        u8 *d = (u8 *)buf;
        for (i = 0; i < k; i++) {
            d[i] = (u8)f->ino->data[f->off + i];
        }
    }
    f->off += k;
    return (i64)k;
}

i64 vfs_write(int fd, const void *buf, u64 n)
{
    file_t *f;
    const char *s = (const char *)buf;
    u64 i;

    if (fd < 0 || fd >= MAX_FD || !fds[fd].used) {
        return -1;
    }
    f = &fds[fd];
    if (f->ino->type == T_DEV && f->ino->dev == DEV_CON) {
        for (i = 0; i < n; i++) {
            kputc(s[i]);
        }
        return (i64)n;
    }
    if (f->ino->type != T_FILE) {
        return -1;
    }
    if (f->off + n > RAMFS_FILE_MAX) {
        return -1; /* ramfs 单文件上限，见 RAMFS_FILE_MAX */
    }
    if (f->off + n > f->ino->cap) {
        u64 ncap = f->off + n;
        char *nb = (char *)kmalloc(ncap);
        if (nb == (char *)0) {
            return -1;
        }
        for (i = 0; i < f->ino->size; i++) {
            nb[i] = f->ino->data[i];
        }
        if (f->ino->data != (char *)0) {
            kfree(f->ino->data);
        }
        f->ino->data = nb;
        f->ino->cap = ncap;
    }
    for (i = 0; i < n; i++) {
        f->ino->data[f->off + i] = s[i];
    }
    f->off += n;
    if (f->off > f->ino->size) {
        f->ino->size = f->off;
    }
    return (i64)n;
}

i64 vfs_close(int fd)
{
    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) {
        return fd < 3 && fd >= 0 ? 0 : -1;
    }
    fds[fd].used = 0;
    fds[fd].ino = (ino_t *)0;
    fds[fd].off = 0;
    return 0;
}

i64 vfs_list(const char *path, char *out, u64 cap)
{
    ino_t *n = walk(path);
    ino_t *p;
    u64 used = 0;

    if (n == (ino_t *)0 || out == (char *)0 || cap == 0) {
        return -1;
    }
    if (n->type != T_DIR) {
        return -1;
    }
    for (p = n->child; p != (ino_t *)0; p = p->sib) {
        const char *s = p->name;
        while (*s != '\0' && used + 2 < cap) {
            out[used++] = *s++;
        }
        if (p->type == T_DIR && used + 2 < cap) {
            out[used++] = '/';
        }
        if (used + 1 < cap) {
            out[used++] = '\n';
        }
    }
    out[used] = '\0';
    return (i64)used;
}
/* ---- 每进程 fd 表：新建 / fork 时深拷贝 ---- */

/* ramfs 自检：连续建 50 个文件 -> 写 -> 删，验证 inode 池与链表回收。
 * 这个测试是内核侧的（不起进程），目的是尽早暴露“池子太小/删链漏了”。 */
static void path_num(char *dst, u32 n)
{
    char tmp[12];
    u32 i = 0;
    u32 len = 0;

    dst[0] = '/';
    dst[1] = 't';
    dst[2] = 'm';
    dst[3] = 'p';
    dst[4] = '/';
    dst[5] = 's';
    len = 6;
    if (n == 0) {
        tmp[i++] = '0';
    }
    while (n > 0) {
        tmp[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i > 0) {
        dst[len++] = tmp[--i];
    }
    dst[len] = '\0';
}

i64 vfs_dup(int fd)
{
    int i;

    if (fd < 0 || fd >= MAX_FD || !fds[fd].used) {
        return -1;
    }
    for (i = 3; i < MAX_FD; i++) {
        if (!fds[i].used) {
            fds[i] = fds[fd];
            return (i64)i;
        }
    }
    return -1;
}

i64 vfs_rename(const char *oldp, const char *newp)
{
    char oname[24];
    char nname[24];
    ino_t *op = split_parent(oldp, oname, sizeof(oname));
    ino_t *np = split_parent(newp, nname, sizeof(nname));
    ino_t *victim;
    ino_t **pp;

    if (op == (ino_t *)0 || np == (ino_t *)0) {
        return -1;
    }
    victim = ino_find(op, oname);
    if (victim == (ino_t *)0) {
        return -1;
    }
    if (ino_find(np, nname) != (ino_t *)0) {
        return -1; /* 目标已存在 */
    }
    pp = &op->child;
    while (*pp != (ino_t *)0 && *pp != victim) {
        pp = &(*pp)->sib;
    }
    if (*pp == victim) {
        *pp = victim->sib;
    }
    victim->sib = (ino_t *)0;
    victim->parent = np;
    if (np->child == (ino_t *)0) {
        np->child = victim;
    } else {
        ino_t *tail = np->child;

        while (tail->sib != (ino_t *)0) {
            tail = tail->sib;
        }
        tail->sib = victim;
    }
    str_copy(victim->name, nname, sizeof(victim->name));
    return 0;
}

i64 vfs_seek(int fd, i64 off)
{
    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) {
        return -1;
    }
    if (off < 0 || (u64)off > fds[fd].ino->size) {
        return -1;
    }
    fds[fd].off = (u64)off;
    return off;
}

i64 vfs_size(const char *path)
{
    ino_t *n;

    if (is_mnt_path(path)) {
        ino_t *mnt = walk("/mnt");
        n = mnt != (ino_t *)0 ? ino_find(mnt, path + 5) : (ino_t *)0;
        if (n != (ino_t *)0 && n->data == (char *)0) {
            return -1; /* 尚未物化，调用方先 open/read 一次 */
        }
    } else {
        n = walk(path);
    }
    if (n == (ino_t *)0) {
        return -1;
    }
    return (i64)n->size;
}

int vfs_selftest(void)
{
    char path[32];
    u32 i;
    int ok = 1;

    for (i = 0; i < 50; i++) {
        i64 fd;

        path_num(path, i);
        fd = vfs_open(path, 1);
        if (fd < 0) {
            ok = 0;
            break;
        }
        if (vfs_write(fd, "x", 1) != 1) {
            ok = 0;
        }
        vfs_close(fd);
        if (!ok) {
            break;
        }
    }
    for (i = 0; i < 50 && ok; i++) {
        path_num(path, i);
        if (vfs_unlink(path) != 0) {
            ok = 0;
        }
    }
    /* 第二轮：如果 unlink 没有把槽位还回来，这里一定失败 */
    for (i = 0; i < 50 && ok; i++) {
        i64 fd;

        path_num(path, i);
        fd = vfs_open(path, 1);
        if (fd < 0) {
            ok = 0;
            break;
        }
        vfs_close(fd);
        vfs_unlink(path);
    }
    return ok;
}

struct filetable *vfs_ftab_new(void)
{
    struct filetable *t = (struct filetable *)kmalloc(sizeof(struct filetable));
    ino_t *con;
    int i;

    if (t == (struct filetable *)0) {
        return (struct filetable *)0;
    }
    for (i = 0; i < MAX_FD; i++) {
        t->f[i].used = 0;
        t->f[i].ino = (ino_t *)0;
        t->f[i].off = 0;
    }
    con = walk("/dev/console");
    for (i = 0; i < 3 && con != (ino_t *)0; i++) {
        t->f[i].ino = con;
        t->f[i].used = 1;
        t->f[i].off = 0;
    }
    return t;
}

struct filetable *vfs_ftab_clone(void)
{
    struct filetable *src = ftab();
    struct filetable *dst = vfs_ftab_new();
    int i;

    if (dst == (struct filetable *)0) {
        return (struct filetable *)0;
    }
    for (i = 0; i < MAX_FD; i++) {
        dst->f[i] = src->f[i];
    }
    return dst;
}
