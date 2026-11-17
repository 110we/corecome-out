/* user.c —— 进 EL0 的那一步。
 *
 * 前面 paging.c 已经把“同一把 VA、不同物理页”准备好了，这里补三件事：
 *   1. 给用户态一份 aspace：代码页 RX、栈页 RW，都落在用户窗里；
 *   2. eret 进 EL0（SPSR=EL0t，DAIF 先全屏蔽，不让定时器在用户态插进来）；
 *   3. 出来只有一条路：svc 进 do_sync，exit 时注销线程，永不回退。
 *
 * 为什么用户程序不链接进内核镜像直接跑：内核 .text 在 0x40000000，
 * 对 EL0 是 EL1-only 的页，用户读一下自己的字符串就 fault。所以代码
 * 必须落到用户窗自己的页里——拷贝，而不是借地址。
 */
#include "kern.h"

extern const u8 user_blob_start[];
extern const u8 user_blob_end[];
extern const u8 user_bad_start[];
extern const u8 user_bad_end[];
extern const u8 user_bad2_start[];
extern const u8 user_bad2_end[];
extern const u8 user_bad3_start[];
extern const u8 user_bad3_end[];
extern const u8 user_init_start[];
extern const u8 user_init_end[];
extern const u8 user_cow_start[];
extern const u8 user_cow_end[];
extern const u8 user_thr_start[];
extern const u8 user_thr_end[];
extern const u8 user_sig_start[];
extern const u8 user_sig_end[];
extern const u8 user_file_start[];
extern const u8 user_access_start[];
extern const u8 user_access_end[];
extern const u8 user_pipe_start[];
extern const u8 user_pipe_end[];
extern const u8 user_ulo_start[];
extern const u8 user_ulo_end[];
extern const u8 user_dup_start[];
extern const u8 user_dup_end[];
extern const u8 user_rename_start[];
extern const u8 user_rename_end[];
extern const u8 user_seek_start[];
extern const u8 user_seek_end[];
extern const u8 user_append_start[];
extern const u8 user_append_end[];
extern const u8 user_trunc_start[];
extern const u8 user_trunc_end[];
extern const u8 user_fork_start[];
extern const u8 user_fork_end[];
extern const u8 user_sleep_start[];
extern const u8 user_sleep_end[];
extern const u8 user_time_start[];
extern const u8 user_time_end[];
extern const u8 user_stat_start[];
extern const u8 user_stat_end[];
extern const u8 user_dir_start[];
extern const u8 user_dir_end[];
extern const u8 user_ep_start[];
extern const u8 user_ep_end[];
extern const u8 user_file_end[];
extern const u8 user_m4_init_start[];
extern const u8 user_m4_init_end[];

/* 进 EL0。返回只可能发生在异常没处理好时——正常路径是 exit 掉。 */
static void user_enter(u64 entry, u64 sp)
{
    __asm__ volatile(
        "msr sp_el0, %0\n"
        "msr elr_el1, %1\n"
        "msr spsr_el1, %2\n"
        "eret\n" ::"r"(sp),
        "r"(entry), "r"(0UL)); /* EL0t，I=0：用户态可被时钟抢占 */
}

static u64 t0;

u64 user_enter_ticks(void)
{
    return t0;
}

static void user_spawn(const u8 *blob, const u8 *blob_end, const char *tag)
{
    u64 *l1;
    u8 *code;
    u8 *stk;
    u64 n;
    u64 i;

    l1 = pg_aspace_new();
    code = (u8 *)mm_page_alloc();
    stk = (u8 *)mm_page_alloc();
    if (l1 == (u64 *)0 || code == (u8 *)0 || stk == (u8 *)0) {
        kputs("[kern] user: out of memory\n");
        pg_activate((u64 *)0);
        kthread_exit();
        return;
    }

    n = (u64)(blob_end - blob);
    if (n > 4096) {
        kputs("[kern] user: blob too big\n");
        pg_activate((u64 *)0);
        kthread_exit();
        return;
    }
    for (i = 0; i < n; i++) {
        code[i] = blob[i];
    }

    if (pg_map_user(l1, USER_WIN_BASE, (u64)code, PG_USER_RX) != 0 ||
        pg_map_user(l1, USER_WIN_BASE + 0x1000, (u64)stk, PG_USER_RW) != 0) {
        kputs("[kern] user: map failed\n");
        pg_activate((u64 *)0);
        kthread_exit();
        return;
    }

    printk("[kern] user: enter el0 tag=%s pc=%x sp=%x bytes=%u\n",
           tag, (u32)USER_WIN_BASE, (u32)(USER_WIN_BASE + 0x2000), (u32)n);

/* 地址空间登记到线程上：从这以后，抢占/恢复由调度器负责切 TTBR0，
 * 所以 EL0 可以放心开着中断跑。 */
    sched_set_l1(l1);
    pg_activate(l1);

    /* 临界区只剩 eret 前的三条 msr：ELR/SPSR/SP_EL0 是系统寄存器，
     * 跨抢占点就会被别人覆盖。出去之后 EL0 用自己的中断状态（SPSR.I=0）。 */
    __asm__ volatile("msr daifset, #2");
    t0 = tick_count();
    user_enter(USER_WIN_BASE, USER_WIN_BASE + 0x2000);

    /* 走到这里说明 eret 之后又回到了 EL1 —— exit 没生效，是 bug。 */
    kputs("[kern] user: eret returned (BUG)\n");
    sched_set_l1((u64 *)0);
    pg_activate((u64 *)0);
    kthread_exit();
}

/* ACCESS 靶子：存在/不存在两条路径。 */
void user_access_thread(void *arg)
{
    (void)arg;
    user_spawn(user_access_start, user_access_end, "access");
}

/* 管道靶子：写入 2 字节再从读端取回。 */
void user_pipe_thread(void *arg)
{
    (void)arg;
    user_spawn(user_pipe_start, user_pipe_end, "pipe");
}

/* unlink-open 靶子：删名之后 fd 仍可读，关闭才释放。 */
void user_ulo_thread(void *arg)
{
    (void)arg;
    user_spawn(user_ulo_start, user_ulo_end, "unlinkopen");
}

/* DUP 靶子：复制 fd 后从副本读。 */
void user_dup_thread(void *arg)
{
    (void)arg;
    user_spawn(user_dup_start, user_dup_end, "dup");
}

/* RENAME 靶子：改名后旧路径消失、新路径可查。 */
void user_rename_thread(void *arg)
{
    (void)arg;
    user_spawn(user_rename_start, user_rename_end, "rename");
}

/* SEEK 靶子：seek(5) 后读到 'f'。 */
void user_seek_thread(void *arg)
{
    (void)arg;
    user_spawn(user_seek_start, user_seek_end, "seek");
}

/* O_APPEND 靶子：追加后大小应为 6。 */
void user_append_thread(void *arg)
{
    (void)arg;
    user_spawn(user_append_start, user_append_end, "append");
}

/* O_TRUNC 靶子：重开清零后 stat 为 0。 */
void user_trunc_thread(void *arg)
{
    (void)arg;
    user_spawn(user_trunc_start, user_trunc_end, "trunc");
}

/* fork/wait 压力靶子：8 个孩子依次退出并被回收。 */
void user_fork_thread(void *arg)
{
    (void)arg;
    user_spawn(user_fork_start, user_fork_end, "forkstress");
}

/* 睡眠靶子：sleep(4) 之后 tick 至少前进 4。 */
void user_sleep_thread(void *arg)
{
    (void)arg;
    user_spawn(user_sleep_start, user_sleep_end, "sleep");
}

/* tick 靶子：两次 SYS_TIME 单调不减。 */
void user_time_thread(void *arg)
{
    (void)arg;
    user_spawn(user_time_start, user_time_end, "time");
}

/* stat 靶子：查文件大小。 */
void user_stat_thread(void *arg)
{
    (void)arg;
    user_spawn(user_stat_start, user_stat_end, "stat");
}

/* 目录操作靶子：mkdir / O_CREAT / write / unlink。 */
void user_dir_thread(void *arg)
{
    (void)arg;
    user_spawn(user_dir_start, user_dir_end, "dirops");
}

/* exec-by-path 靶子：fork 后孩子从 ramfs 加载镜像。 */
void user_ep_thread(void *arg)
{
    (void)arg;
    user_spawn(user_ep_start, user_ep_end, "execfile");
}

/* 读文件程序：走 VFS open/read，把文件内容打到控制台。 */
void user_file_thread(void *arg)
{
    (void)arg;
    user_spawn(user_file_start, user_file_end, "file");
}

/* 正常程序：打印一行，exit 0。 */
void user_thread(void *arg)
{
    (void)arg;
    user_spawn(user_blob_start, user_blob_end, "good");
}

/* 越界程序：伸手读 0x40000000（内核镜像，EL1-only），必须被拦下。 */
void user_bad_thread(void *arg)
{
    (void)arg;
    user_spawn(user_bad_start, user_bad_end, "bad");
}

/* 第三层的靶子：参数是内核地址，必须被 syscall 层拒绝。 */
void user_bad2_thread(void *arg)
{
    (void)arg;
    user_spawn(user_bad2_start, user_bad2_end, "deny");
}

/* 第四个靶子：栈上执行。UXN 挡住的必须是指令中止，不是数据中止。 */
void user_bad3_thread(void *arg)
{
    (void)arg;
    user_spawn(user_bad3_start, user_bad3_end, "nx");
}

/* ---- M4：init 拉起 shell ----
 * init 是 EL0 程序：fork 后孩子 exec(1)=shell，父亲 wait 到 shell 退出。
 * g_shell_mode 让内核命令行把串口让给 shell；init0 退出时在 kthread_exit 里清。 */
void user_m4_thread(void *arg)
{
    (void)arg;
    g_shell_mode = 1;
    user_spawn(user_m4_init_start, user_m4_init_end, "m4init");
}

/* ---- 进程语义的靶子：fork -> 孩子 exec -> 父亲 wait ---- */
void user_init_thread(void *arg)
{
    (void)arg;
    user_spawn(user_init_start, user_init_end, "init");
}

/* ---- COW 的靶子：fork 后父子共享页，写时才分家 ---- */
void user_cow_thread(void *arg)
{
    (void)arg;
    user_spawn(user_cow_start, user_cow_end, "cow");
}

/* ---- 线程的靶子：clone 共享地址空间 ---- */
void user_thr_thread(void *arg)
{
    (void)arg;
    user_spawn(user_thr_start, user_thr_end, "thr");
}

/* ---- 信号的靶子：kill 子进程，退出码 -15 ---- */
void user_sig_thread(void *arg)
{
    (void)arg;
    user_spawn(user_sig_start, user_sig_end, "sig");
}
