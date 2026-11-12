/* vfs.c —— 大内核里的文件层：inode 树 + 打开表 + ramfs + /dev/console。
 *
 * 没有块设备、没有缓存、没有并发保护：先用最小实现证明
 * “用户态要的文件语义，在内核里是一条函数调用链”。
 */
#include "kern.h"

#define MAX_INO   24
#define MAX_FD    16
#define T_DIR     0
#define T_FILE    1
#define T_DEV     2
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

static ino_t *ino_new(ino_t *parent, const char *name, int type)
{
    ino_t *n;

    if (np >= MAX_INO) {
        return (ino_t *)0;
    }
    n = &pool[np++];
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

static void set_text(ino_t *n, const char *s)
{
    u64 len = 0;
    char *buf;

    while (s[len] != '\0') {
        len++;
    }
    if (len > 1024) {
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

i64 vfs_open(const char *path, int flags)
{
    ino_t *n = walk(path);
    int i;

    (void)flags;
    if (n == (ino_t *)0) {
        return -1;
    }
    for (i = 3; i < MAX_FD; i++) {
        if (!fds[i].used) {
            fds[i].ino = n;
            fds[i].off = 0;
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
    if (f->ino->type != T_FILE) {
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
    if (f->off + n > 1024) {
        return -1; /* ramfs 单文件上限 1 KiB，够自检用 */
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
