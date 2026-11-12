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
