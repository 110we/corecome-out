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
#define ST_ZOMBIE 4      /* 已退出，等父进程 wait 收尸 */

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
    u64 ftab;               /* 进程私有 fd 表（vfs.c 的 struct filetable） */
    int pid;
    int ppid;
    int tgid;               /* 线程组：clone 出来的线程与创建者同组 */
    u64 sig_pending;        /* 待投递信号：位图，bit n = signal n */
    u64 code;               /* 退出码：僵尸态留给父进程收 */
} tcb_t;

extern void switch_to(tcb_t *prev, tcb_t *next);
extern void thread_boot(void);

static tcb_t tcbs[KTHREAD_MAX];
static u8 stacks[KTHREAD_MAX][STACK_SZ] __attribute__((aligned(16)));
static int nth;
static int next_pid = 1;   /* 0 号是 kmain，用户进程从 1 开始发号 */
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
        tcbs[i].ftab = 0;
        tcbs[i].pid = -1;
        tcbs[i].ppid = -1;
        tcbs[i].tgid = -1;
        tcbs[i].sig_pending = 0;
        tcbs[i].code = 0;
    }
    nth = 1;
    tcbs[0].idx = 0;
    tcbs[0].pid = 0;
    tcbs[0].ppid = -1;
    tcbs[0].tgid = 0;
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
    t->pid = next_pid++;
    t->ppid = 0;
    t->tgid = (int)t->pid;
    t->sig_pending = 0;
    t->code = 0;
    t->ftab = 0;
    t->l1 = (u64 *)0;
    t->daif = 0;
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

extern volatile int g_shell_mode;

void kthread_exit(void)
{
    /* init0 退出 = EL0 的 init 收摊，把串口还给内核命令行。 */
    if (cur != (tcb_t *)0 && cur->name[0] == 'i' && cur->name[1] == 'n' &&
        cur->name[2] == 'i' && cur->name[3] == 't' && cur->name[4] == '0') {
        g_shell_mode = 0;
    }
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

/* ---- 进程原语：fork / 僵尸 / wait / 每进程 fd 表 ---- */

/* 所有“新执行流”都收敛到这里：给 frame、asm、fd 表、父子关系，
 * 剩下的（内核栈、first-run 路径）由它统一铺。
 * 关键那条：第一次被切到必须走 thread_boot；若照抄用户的 x30，
 * ret 会跳回调用者的返回路径，fork 就变成“无限分身”。 */
static int sched_spawn(u64 *f, u64 *l1, u64 ftab, const char *nm,
                       int ppid, int tgid)
{
    tcb_t *t;
    u64 *frame;
    int i;

    if (nth >= KTHREAD_MAX || cur == (tcb_t *)0) {
        return -1;
    }
    t = &tcbs[nth];
    t->idx = (u64)nth;
    name_copy(t->name, nm);
    t->state = ST_READY;
    t->slice = SLICE;
    t->pid = next_pid++;
    t->ppid = ppid;
    t->tgid = tgid > 0 ? tgid : (int)t->pid;
    t->sig_pending = 0;
    t->code = 0;
    t->ftab = ftab;
    t->l1 = l1;
    t->daif = 0;
    t->fp = f[29];
    t->lr = (u64)&thread_boot;
    frame = (u64 *)((u8 *)&stacks[nth][0] + STACK_SZ - 288);
    for (i = 0; i < 36; i++) {
        frame[i] = f[i];
    }
    t->sp = (u64)frame;
    nth++;
    return t->pid;
}

/* 从父进程的 trap frame 造一个孩子：frame 原样抄，x0=0（fork 在孩子里返回 0）。 */
int sched_fork_from(u64 *f, u64 *child_l1, u64 ftab, const char *nm)
{
    u64 g[36];
    int i;

    if (cur == (tcb_t *)0) {
        return -1;
    }
    for (i = 0; i < 36; i++) {
        g[i] = f[i];
    }
    g[0] = 0;              /* 孩子的 fork() 返回 0 */
    return sched_spawn(g, child_l1, ftab, nm, (int)cur->pid, 0);
}

/* clone(CLONE_VM|CLONE_THREAD)：共用同一个 aspace 与 fd 表，
 * 只换一段入口和新栈。线程退出不复用父的收尸路径之外的东西。 */
int sched_clone_thread(u64 *f, u64 *l1, u64 ftab, u64 entry, u64 arg, u64 sp)
{
    u64 g[36];
    int i;

    if (cur == (tcb_t *)0) {
        return -1;
    }
    for (i = 0; i < 36; i++) {
        g[i] = f[i];
    }
    g[0] = arg;
    g[30] = 0;
    g[32] = entry;         /* ELR：线程从 entry 开始 */
    g[33] = 0;             /* SPSR=EL0t */
    g[34] = sp;            /* SP_EL0：线程自己的栈 */
    return sched_spawn(g, l1, ftab, "thr", (int)cur->pid, (int)cur->tgid);
}

int sched_tgid(void)
{
    return cur == (tcb_t *)0 ? -1 : (int)cur->tgid;
}

/* 用户态 exit：变成僵尸，把退出码留在 tcb 上，等父进程来收。
 * 线程也一样进僵尸（父 wait(tid) 能取到 code），区别只在收尸时
 * 不拆 aspace——那由线程组最后一个人负责。 */
void sched_exit_zombie(int code)
{
    cur->code = (u64)code;
    cur->sig_pending = 0;
    cur->state = ST_ZOMBIE;
    schedule();
    panic("sched: zombie returned");
}

/* ---- 信号：P0 子集 ----
 * 只有默认动作：KILL/TERM 终止，SEGV 终止，CHLD 忽略。
 * 没有用户态 handler、没有 sigreturn —— 那要把信号帧铺到用户栈上，
 * 是下一层的事。这一层先把“能被打断、退出码 = -signum”做对：
 * 投递点是 (1) 时钟中断回来时 (2) 系统调用返回前，两处都是安全点。
 */
#define SIGSEGV 11
#define SIGKILL 9
#define SIGTERM 15

int sched_kill(int pid, int sig)
{
    int i;

    if (sig <= 0 || sig > 63) {
        return -1;
    }
    for (i = 0; i < nth; i++) {
        tcb_t *t = &tcbs[i];

        if (t->pid != pid || t->state == ST_FREE || t->state == ST_ZOMBIE) {
            continue;
        }
        if (sig == SIGKILL || sig == SIGTERM || sig == SIGSEGV) {
            t->sig_pending |= 1UL << sig;
        }
        printk("[kern] sign: kill pid=%u sig=%u\n", (u32)pid, (u32)sig);
        return 0;
    }
    return -1;
}

/* 当前线程该不该死：返回信号号（0 = 没有）。 */
int sched_signal_self(void)
{
    if (cur == (tcb_t *)0) {
        return 0;
    }
    if ((cur->sig_pending & (1UL << SIGKILL)) != 0) {
        return SIGKILL;
    }
    if ((cur->sig_pending & (1UL << SIGTERM)) != 0) {
        return SIGTERM;
    }
    if ((cur->sig_pending & (1UL << SIGSEGV)) != 0) {
        return SIGSEGV;
    }
    return 0;
}

/* 收尸：只收自己的孩子（pid==-1 收任意）。返回 pid，0 表示还没有僵尸。 */
int sched_wait_child(int pid, int *code)
{
    int i;

    if (cur == (tcb_t *)0) {
        return 0;
    }
    for (i = 0; i < nth; i++) {
        tcb_t *t = &tcbs[i];

        if (t->state != ST_ZOMBIE || t->ppid != (int)cur->pid) {
            continue;
        }
        if (pid >= 0 && t->pid != pid) {
            continue;
        }
        *code = (int)t->code;
        t->state = ST_FREE;
        t->ppid = -1;
        /* 收尸时才还地址空间引用。fork 的进程引用只有 1 次，这里直接拆；
         * clone 的线程共享同一份，refs>1 时只减一，最后一个才真拆。 */
        pg_aspace_unref(t->l1);
        t->l1 = (u64 *)0;
        return t->pid;
    }
    return 0;
}

int sched_has_child(void)
{
    int i;

    for (i = 0; i < nth; i++) {
        if (tcbs[i].ppid == (int)cur->pid && tcbs[i].state != ST_FREE) {
            return 1;
        }
    }
    return 0;
}

int sched_pid(void)
{
    return cur == (tcb_t *)0 ? -1 : (int)cur->pid;
}

int sched_ppid(void)
{
    return cur == (tcb_t *)0 ? -1 : (int)cur->ppid;
}

u64 sched_get_ftab(void)
{
    return cur == (tcb_t *)0 ? 0 : cur->ftab;
}

void sched_set_ftab(u64 ftab)
{
    if (cur != (tcb_t *)0) {
        cur->ftab = ftab;
    }
}

/* ps：进程视角的一行一条 */
void ps_dump(void)
{
    static const char *st[5] = { "free", "ready", "run", "exit", "zombie" };
    int i;

    printk("PID PPID STATE  CODE NAME\n");
    for (i = 0; i < nth; i++) {
        tcb_t *t = &tcbs[i];

        if (t->pid < 0) {
            continue;
        }
        printk("%u %u %s %u %s\n", (u32)t->pid,
               (u32)(t->ppid < 0 ? 0 : t->ppid),
               (t->state >= 0 && t->state <= 4) ? st[t->state] : "?",
               (u32)t->code, t->name);
    }
}
void sched_dump(void)
{
    static const char *st[5] = { "free", "ready", "run", "exit", "zombie" };
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
               tcbs[i].name,
               (tcbs[i].state >= 0 && tcbs[i].state <= 4) ? st[tcbs[i].state]
                                                          : "?",
               (u32)tcbs[i].slice, (u32)tcbs[i].sp, (u32)(u64)tcbs[i].l1);
    }
}
