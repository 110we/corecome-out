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

/* 进 EL0。返回只可能发生在异常没处理好时——正常路径是 exit 掉。 */
static void user_enter(u64 entry, u64 sp)
{
    __asm__ volatile(
        "msr sp_el0, %0\n"
        "msr elr_el1, %1\n"
        "msr spsr_el1, %2\n"
        "eret\n" ::"r"(sp),
        "r"(entry), "r"(0x3C0UL)); /* EL0t + DAIF 全掩 */
}

void user_thread(void *arg)
{
    u64 *l1;
    u8 *code;
    u8 *stk;
    u64 n;
    u64 i;

    (void)arg;

    l1 = pg_aspace_new();
    code = (u8 *)mm_page_alloc();
    stk = (u8 *)mm_page_alloc();
    if (l1 == (u64 *)0 || code == (u8 *)0 || stk == (u8 *)0) {
        kputs("[kern] user: out of memory\n");
        kthread_exit();
        return;
    }

    n = (u64)(user_blob_end - user_blob_start);
    if (n > 4096) {
        kputs("[kern] user: blob too big\n");
        kthread_exit();
        return;
    }
    for (i = 0; i < n; i++) {
        code[i] = (u8)user_blob_start[i];
    }

    if (pg_map_user(l1, USER_WIN_BASE, (u64)code, PG_USER_RX) != 0 ||
        pg_map_user(l1, USER_WIN_BASE + 0x1000, (u64)stk, PG_USER_RW) != 0) {
        kputs("[kern] user: map failed\n");
        kthread_exit();
        return;
    }

    printk("[kern] user: enter el0 pc=%x sp=%x bytes=%u\n",
           (u32)USER_WIN_BASE, (u32)(USER_WIN_BASE + 0x2000), (u32)n);

    pg_activate(l1);
    user_enter(USER_WIN_BASE, USER_WIN_BASE + 0x2000);

    /* 走到这里说明 eret 之后又回到了 EL1 —— exit 没生效，是 bug。 */
    kputs("[kern] user: eret returned (BUG)\n");
    pg_activate((u64 *)0);
    kthread_exit();
}