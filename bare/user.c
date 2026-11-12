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

/* ---- 进程语义的靶子：fork -> 孩子 exec -> 父亲 wait ---- */
void user_init_thread(void *arg)
{
    (void)arg;
    user_spawn(user_init_start, user_init_end, "init");
}
