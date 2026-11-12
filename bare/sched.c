/* sched.c —— 内核线程 + 100 Hz 轮转。
 *
 * 不做进程、不做地址空间切换：大内核里调度单位就是内核线程，
 * 所有线程共用同一张页表、同一份内核镜像。
 * 切换点只有一个：switch_to（switch.S）。
 */
#include "kern.h"

#define STACK_SZ  8192UL
#define SLICE     5      /* 5 拍 = 50 ms */

#define ST_FREE   0
#define ST_READY  1
#define ST_RUN     2
#define ST_EXIT   3

typedef struct tcb {
    u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    u64 fp;                 /* x29 */
    u64 lr;                 /* x30 */
    u64 sp;
    u64 idx;
    char name[16];
    int state;
    u64 slice;
    u64 daif;               /* 线程私有的中断屏蔽状态 */
    u64 *l1;                /* TTBR0：0 = 内核模板页表 */
} tcb_t;

extern void switch_to(tcb_t *prev, tcb_t *next);
extern void thread_boot(void);

static tcb_t tcbs[KTHREAD_MAX];
static u8 stacks[KTHREAD_MAX][STACK_SZ] __attribute__((aligned(16)));
static int nth;
static tcb_t *cur;
static int sched_on;

static void name_copy(char *d, const char *s)
{
    int i = 0;
    while (s[i] != '\0' && i < 15) {
        d[i] = s[i];
        i++;
    }
    d[i] = '\0';
}

void sched_init(void)
{
    int i;
    for (i = 0; i < KTHREAD_MAX; i++) {
        tcbs[i].state = ST_FREE;
        tcbs[i].slice = SLICE;
        tcbs[i].l1 = (u64 *)0;
        tcbs[i].daif = 0;
    }
    nth = 1;
    tcbs[0].idx = 0;
    name_copy(tcbs[0].name, "kmain");
    tcbs[0].state = ST_RUN;
    cur = &tcbs[0];
    sched_on = 0;
}

int kthread_create(const char *name, void (*fn)(void *), void *arg)
{
    tcb_t *t;
    u64 *frame;
    u64 i;

    if (nth >= KTHREAD_MAX || fn == (void (*)(void *))0) {
        return -1;
    }
    t = &tcbs[nth];
    t->idx = (u64)nth;
    name_copy(t->name, name);
    t->state = ST_READY;
    t->slice = SLICE;
    t->fp = 0;
    t->lr = (u64)&thread_boot;

    /* 栈顶伪造一份 trap frame：x0=arg，x30=0，elr=entry，spsr=EL1h */
    frame = (u64 *)((u8 *)&stacks[nth][0] + STACK_SZ - 288);
    for (i = 0; i < 36; i++) {
        frame[i] = 0;
    }
    frame[0] = (u64)arg;
    frame[32] = (u64)fn;
    frame[33] = 0x5UL;
    t->sp = (u64)frame;

    nth++;
    return (int)t->idx;
}

void sched_go(void)
{
    sched_on = 1;
}

void schedule(void)
{
    tcb_t *prev = cur;
    tcb_t *next = (tcb_t *)0;
    int i;

    if (sched_on == 0) {
        return;
    }
    for (i = 0; i < nth; i++) {
        int j = (int)((prev->idx + 1 + (u64)i) % (u64)nth);
        if (tcbs[j].state == ST_READY) {
            next = &tcbs[j];
            break;
        }
    }
    if (next == (tcb_t *)0) {
        if (prev->state == ST_RUN) {
            return; /* 只剩自己，继续跑 */
        }
        panic("sched: no runnable");
    }
    if (next == prev) {
        return;
    }

    next->state = ST_RUN;
    if (prev->state == ST_RUN) {
        prev->state = ST_READY;
    }
    prev->slice = SLICE;
    cur = next;
    /* 地址空间也跟着线程走：两个用户线程互相抢占时，TTBR0 必须是

     * 即将运行的那个人的。内核在所有 aspace 里都恒等映射，所以
     * 在切换的栈上改 TTBR0 是安全的。 */
    if (next->l1 != prev->l1) {
        pg_activate(next->l1);
    }
    switch_to(prev, next);
}

void sched_tick(void)
{
    if (sched_on == 0 || cur == (tcb_t *)0) {
        return;
    }
    if (cur->slice > 0) {
        cur->slice--;
    }
    if (cur->slice == 0) {
        schedule();
    }
}

void kthread_exit(void)
{
    cur->state = ST_EXIT;
    schedule();
    panic("sched: exit returned");
}

u64 kthread_alive(void)
{
    u64 n = 0;
    int i;
    for (i = 1; i < nth; i++) {
        if (tcbs[i].state == ST_READY || tcbs[i].state == ST_RUN) {
            n++;
        }
    }
    return n;
}

const char *kthread_cur(void)
{
    return cur == (tcb_t *)0 ? "none" : cur->name;
}
/* ---- 地址空间归属：供 user.c / syscall.c 查询当前线程 ---- */
u64 *sched_get_l1(void)
{
    return cur == (tcb_t *)0 ? (u64 *)0 : cur->l1;
}

void sched_set_l1(u64 *l1)
{
    if (cur != (tcb_t *)0) {
        cur->l1 = l1;
    }
}

/* ---- kstruct：把线程表原样倒出来 ---- */
void sched_dump(void)
{
    static const char *st[4] = { "free", "ready", "run", "exit" };
    u64 live = 0;
    u64 created = 0;
    int i;

    for (i = 0; i < nth; i++) {
        if (tcbs[i].state != ST_FREE) {
            created++;
        }
        if (tcbs[i].state == ST_READY || tcbs[i].state == ST_RUN) {
            live++;
        }
    }
    printk("[sched] live=%u created=%u cur=%s\n", (u32)live, (u32)created,
           kthread_cur());
    for (i = 0; i < nth; i++) {
        printk("  #%u %s state=%s slice=%u sp=%x l1=%x\n", (u32)i,
               tcbs[i].name, st[tcbs[i].state & 3], (u32)tcbs[i].slice,
               (u32)tcbs[i].sp, (u32)(u64)tcbs[i].l1);
    }
}
