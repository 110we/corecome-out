/* proc.c —— 进程语义：fork / exec / wait / 退出码。
 *
 * 和 Linux 的差别写在明处：
 *   - 没有写时复制（COW）：fork 直接把父进程的每一页复制一份；
 *   - 没有真正的睡眠队列：wait 是“让出 CPU 再看一眼”，不是 wait_event；
 *   - 没有信号、没有进程组、没有会话。
 * 但语义顺序是一致的：fork 子返回 0、父拿 pid；exec 换掉整个地址空间；
 * 退出进僵尸；父 wait 取 code 并收尸。
 */
#include "kern.h"

extern const u8 user_exec_start[];
extern const u8 user_exec_end[];
extern const u8 user_shell_start[];
extern const u8 user_shell_end[];

static void proc_name(char *d, int pid)
{
    int i = 0;
    char t[8];
    int n = 0;

    t[n++] = 'p';
    if (pid >= 10) {
        t[n++] = (char)('0' + (pid / 10) % 10);
    }
    t[n++] = (char)('0' + pid % 10);
    while (i < n) {
        d[i] = t[i];
        i++;
    }
    d[i] = '\0';
}

int proc_fork(u64 *f)
{
    u64 *l1 = pg_aspace_clone(sched_get_l1());
    struct filetable *ft = vfs_ftab_clone();
    char nm[8];
    int pid;

    if (l1 == (u64 *)0 || ft == (struct filetable *)0) {
        printk("[kern] proc: fork failed (mem)\n");
        return -1;
    }
    proc_name(nm, sched_pid() + 1);
    pid = sched_fork_from(f, l1, (u64)ft, nm);
    if (pid < 0) {
        return -1;
    }
    printk("[kern] proc: fork pid=%u ppid=%u\n", (u32)pid, (u32)sched_pid());
    return pid;
}

/* clone：和 fork 的差别只有“地址空间共不共享”。
 * 这里只加引用不复制页——线程看得见父进程的每一页，写进去也是同一份。
 * 参数：x0=flags、x1=entry、x2=arg、x3=新栈顶。 */
int proc_clone(u64 *f)
{
    u64 *l1 = sched_get_l1();
    int tid;

    if (l1 == (u64 *)0) {
        printk("[kern] proc: clone without aspace\n");
        return -1;
    }
    pg_aspace_ref(l1);
    tid = sched_clone_thread(f, l1, sched_get_ftab(), f[1], f[2], f[3]);
    if (tid < 0) {
        pg_aspace_unref(l1);
        return -1;
    }
    printk("[kern] proc: clone tid=%u tgid=%u entry=%x sp=%x\n",
           (u32)tid, (u32)sched_tgid(), (u32)f[1], (u32)f[3]);
    return tid;
}

/* exec：扔掉旧地址空间，装一个新程序，然后让 do_sync 的返回路径 eret 进去。 */
void proc_exec(u64 *f, u64 id)
{
    u64 *l1;
    u8 *code;
    u8 *stk;
    const u8 *blob = user_exec_start;
    const u8 *bend = user_exec_end;
    u64 n;
    u64 i;

    /* M4：exec 还不认 ELF，只按 id 选内置镜像。
     * 0 = 旧的自检程序，1 = shell。 */
    if (id == 1) {
        blob = user_shell_start;
        bend = user_shell_end;
    }
    l1 = pg_aspace_new();
    code = (u8 *)mm_page_alloc();
    stk = (u8 *)mm_page_alloc();
    if (l1 == (u64 *)0 || code == (u8 *)0 || stk == (u8 *)0) {
        printk("[kern] proc: exec failed (mem)\n");
        return;
    }
    n = (u64)(bend - blob);
    if (n > 4096) {
        n = 4096;
    }
    for (i = 0; i < n; i++) {
        code[i] = blob[i];
    }
    if (pg_map_user(l1, USER_WIN_BASE, (u64)code, PG_USER_RX) != 0 ||
        pg_map_user(l1, USER_WIN_BASE + 0x1000, (u64)stk, PG_USER_RW) != 0) {
        printk("[kern] proc: exec map failed\n");
        return;
    }
    sched_set_l1(l1);
    pg_activate(l1);
    for (i = 0; i < 36; i++) {
        f[i] = 0;
    }
    f[32] = USER_WIN_BASE;                 /* elr */
    f[33] = 0;                             /* spsr: EL0t */
    f[34] = USER_WIN_BASE + 0x2000;        /* sp_el0 */
    printk("[kern] proc: exec pid=%u pc=%x sp=%x\n", (u32)sched_pid(),
           (u32)USER_WIN_BASE, (u32)(USER_WIN_BASE + 0x2000));
}

/* wait：pid=-1 收任意孩子。没有孩子/没有僵尸就返回 -1。
 * 有孩子但还没退，就让出 CPU 再看——这是 yield 轮询，不是睡眠队列。 */
u64 proc_wait(u64 *f)
{
    int want = (int)f[0];
    int code = 0;
    int r;

    for (;;) {
        r = sched_wait_child(want, &code);
        if (r > 0) {
            printk("[kern] proc: wait pid=%u code=%u\n", (u32)r, (u32)code);
            return (u64)(i64)code;
        }
        if (sched_has_child() == 0) {
            printk("[kern] proc: wait no child\n");
            return (u64)-1;
        }
        schedule();
    }
}

/* 从用户态指针拷一个路径进内核缓冲：逐字节过页表，防止用户递内核地址。 */
static int u_copy_path(char *dst, u64 uaddr, u64 cap)
{
    u64 *l1 = sched_get_l1();
    u64 i;

    if (l1 == (u64 *)0 || uaddr < USER_WIN_BASE || uaddr >= USER_WIN_TOP) {
        return -1;
    }
    for (i = 0; i + 1 < cap; i++) {
        u64 va = uaddr + i;
        char c;

        if (pg_user_pa(l1, va) == 0) {
            return -1;
        }
        c = *(const char *)va;
        dst[i] = c;
        if (c == '\0') {
            return 0;
        }
    }
    return -1;
}

/* ELF 小工具（小端） */
static u32 elf_u32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 elf_u64(const u8 *p)
{
    return (u64)elf_u32(p) | ((u64)elf_u32(p + 4) << 32);
}

static u16 elf_u16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

/* 把 ELF64 的 PT_LOAD 段逐页装进用户窗；成功返回 0 并带出入口地址。 */
static int elf_load(u64 *l1, const u8 *img, u64 len, u64 *entry)
{
    u64 phoff;
    u64 entry_v;
    u64 phnum;
    u64 phentsize;
    u64 i;

    if (len < 64 || img[0] != 0x7f || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') {
        return -1;
    }
    if (img[4] != 2 || img[5] != 1) {
        return -1;
    }
    entry_v = elf_u64(img + 24);
    phoff = elf_u64(img + 32);
    phentsize = elf_u16(img + 54);
    phnum = elf_u16(img + 56);
    if (phentsize < 56 || phnum == 0 || phoff + phnum * phentsize > len) {
        return -1;
    }
    for (i = 0; i < phnum; i++) {
        const u8 *ph = img + phoff + i * phentsize;
        u32 type = elf_u32(ph);
        u32 flags = elf_u32(ph + 4);
        u64 off = elf_u64(ph + 8);
        u64 vaddr = elf_u64(ph + 16);
        u64 filesz = elf_u64(ph + 32);
        u64 memsz = elf_u64(ph + 40);
        u64 page_va;
        u64 page_count;
        u64 p;
        u64 attr;

        if (type != 1) {
            continue;
        }
        if (memsz == 0 || off + filesz > len) {
            return -1;
        }
        /* 链接器会额外生成只装 ELF 头的段（地址在 0x400000 一带），
         * 那种段运行期用不到，跳过；真正要加载的段必须落在用户窗内。 */
        if (vaddr < USER_WIN_BASE || vaddr + memsz > USER_WIN_TOP - 0x1000UL) {
            continue;
        }
        attr = PG_USER_RO;
        if ((flags & 2) != 0) {
            attr = PG_USER_RW;
        } else if ((flags & 1) != 0) {
            attr = PG_USER_RX;
        }
        page_va = vaddr & ~0xFFFUL;
        page_count = ((vaddr - page_va) + memsz + 4095UL) / 4096UL;
        for (p = 0; p < page_count; p++) {
            u8 *pg = (u8 *)mm_page_alloc();
            u64 base = page_va + p * 4096UL;
            u64 k;

            if (pg == (u8 *)0) {
                return -1;
            }
            for (k = 0; k < 4096UL; k++) {
                u64 va = base + k;

                if (va >= vaddr && va < vaddr + filesz) {
                    u64 src = off + (va - vaddr);

                    if (src < len) {
                        pg[k] = img[src];
                    }
                }
            }
            if (pg_map_user(l1, base, (u64)pg, attr) != 0) {
                return -1;
            }
        }
    }
    *entry = entry_v;
    return 0;
}

/* exec-by-path：从 ramfs 读镜像进新 aspace，再改 trap frame 返回 EL0。
 * 认两种格式：ELF64（PT_LOAD）与裸代码（固定入口）。 */
void proc_exec_path(u64 *f, u64 upath)
{
    char path[64];
    u64 *l1;
    u8 *code;
    u8 *stk;
    i64 fd;
    i64 n;
    u64 i;
    u64 entry = USER_WIN_BASE;
    int is_elf = 0;
    u64 sp;

    if (u_copy_path(path, upath, sizeof(path)) != 0) {
        printk("[kern] proc: exec path bad ptr\n");
        return;
    }
    l1 = pg_aspace_new();
    code = (u8 *)mm_page_alloc();
    if (l1 == (u64 *)0 || code == (u8 *)0) {
        printk("[kern] proc: exec-file failed (mem)\n");
        return;
    }
    fd = vfs_open(path, 0);
    if (fd < 0) {
        printk("[kern] proc: exec-file open failed path=%s\n", path);
        return;
    }
    n = vfs_read(fd, code, 4096);
    vfs_close(fd);
    if (n <= 0) {
        printk("[kern] proc: exec-file read failed path=%s\n", path);
        return;
    }
    if (n >= 64 && code[0] == 0x7f && code[1] == 'E' && code[2] == 'L' && code[3] == 'F') {
        if (elf_load(l1, code, (u64)n, &entry) != 0) {
            printk("[kern] proc: exec-file bad ELF path=%s\n", path);
            return;
        }
        is_elf = 1;
        stk = (u8 *)mm_page_alloc();
        if (stk == (u8 *)0 ||
            pg_map_user(l1, USER_WIN_TOP - 0x1000UL, (u64)stk, PG_USER_RW) != 0) {
            printk("[kern] proc: exec-file stack map failed\n");
            return;
        }
        sp = USER_WIN_TOP;
    } else {
        stk = (u8 *)mm_page_alloc();
        if (stk == (u8 *)0 ||
            pg_map_user(l1, USER_WIN_BASE, (u64)code, PG_USER_RX) != 0 ||
            pg_map_user(l1, USER_WIN_BASE + 0x1000, (u64)stk, PG_USER_RW) != 0) {
            printk("[kern] proc: exec-file map failed\n");
            return;
        }
        sp = USER_WIN_BASE + 0x2000;
    }
    sched_set_l1(l1);
    pg_activate(l1);
    for (i = 0; i < 34; i++) {
        f[i] = 0;
    }
    f[32] = entry;
    f[33] = 0;
    f[34] = sp;
    printk("[kern] proc: exec-file pid=%u path=%s bytes=%u %s entry=%x\n",
           (u32)sched_pid(), path, (u32)n, is_elf ? "elf" : "raw", entry);
}
