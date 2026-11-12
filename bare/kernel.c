/* kernel.c —— 内核主体（P0）。
 * 异常向量在 vectors.S，这里只做现场解释；GIC/定时器/MMU/UART 收全部自理。
 * 没有 libc、没有浮点。出错要能看见 ESR/FAR，不许静默重启。
 */

#include "kern.h"

#define UART0_BASE 0x09000000UL
#define GICD_BASE  0x08000000UL
#define GICC_BASE  0x08010000UL

#define UART_DR   0x00
#define UART_FR   0x18
#define UART_IMSC 0x38
#define UART_ICR  0x44

#define FR_TXFF (1u << 5)
#define FR_RXFE (1u << 4)

static volatile u32 *const uart = (volatile u32 *)UART0_BASE;
static volatile u32 *const gicd = (volatile u32 *)GICD_BASE;
static volatile u32 *const gicc = (volatile u32 *)GICC_BASE;

static void uart_putc(char c)
{
    while (uart[UART_FR / 4] & FR_TXFF) {
    }
    uart[UART_DR / 4] = (u32)(u8)c;
}

static void puts_(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

static void put_hex(u64 v)
{
    static const char d[] = "0123456789abcdef";
    int i;

    puts_("0x");
    for (i = 15; i >= 0; i--) {
        uart_putc(d[(v >> (i * 4)) & 0xf]);
    }
}

static void put_dec(u64 v)
{
    char b[21];
    int i = 0;

    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0 && i < 20) {
        b[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        uart_putc(b[--i]);
    }
}

void printk(const char *f, ...)
{
    __builtin_va_list ap;

    __builtin_va_start(ap, f);
    for (; *f != '\0'; f++) {
        if (*f != '%') {
            uart_putc(*f);
            continue;
        }
        f++;
        switch (*f) {
        case 's':
            puts_(__builtin_va_arg(ap, const char *));
            break;
        case 'x':
            put_hex(__builtin_va_arg(ap, u64));
            break;
        case 'u':
            put_dec(__builtin_va_arg(ap, u64));
            break;
        case 'c':
            uart_putc((char)__builtin_va_arg(ap, int));
            break;
        case '%':
            uart_putc('%');
            break;
        default:
            uart_putc('%');
            uart_putc(*f);
            break;
        }
    }
    __builtin_va_end(ap);
}

void panic(const char *m)
{
    __asm__ volatile("msr daifset, #0xf");
    printk("\n[panic] %s\n", m);
    for (;;) {
        __asm__ volatile("wfe");
    }
}

/* ---- 异常现场 ---- */
static volatile u64 ticks;
static volatile u64 svc_seen;
static volatile u64 fault_seen;

extern u64 syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2);

u64 tick_count(void)
{
    return ticks;
}

void kputc(char c)
{
    uart_putc(c);
}

void kputs(const char *s)
{
    puts_(s);
}

static void timer_arm(void);
static void uart_rx_isr(void);

/* 当前系统调用是不是从 EL0 进来的——syscall 层要靠它决定是否校验用户指针。
 * 内核线程自己 svc 时传的是内核指针，不能套用户窗口那套规则。 */
volatile int g_from_el0;

void do_sync(u64 *f)
{
    u64 esr, elr, far;
    u32 ec;

    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    ec = (u32)(esr >> 26);

    /* 先改返回地址再 printk：frame 指针可能只是临时寄存器，printk 会踩它 */
    if (ec == 0x15) { /* SVC from AArch64：ELR 已是下一条，不能再 +4 */
        u32 imm = (u32)(esr & 0xFFFF);
        /* 从 EL0 进来时 SPSR 是 EL0t；内核线程自己 svc 则是 EL1h。 */
        int from_el0 = ((f[33] & 0xFUL) == 0);
        if (imm == 0x80) { /* S0：系统调用门 */
            /* exit 只对 EL0 特殊处理：注销线程，并且把 TTBR0 切回模板，
             * 免得下一个内核线程沿用一份已经作废的用户地址空间。 */
            if (from_el0 && f[8] == 9) { /* SYS_EXIT */
                /* preempt = 用户态跑这段时间里时钟走了几拍。>0 才说明
                 * EL0 真的开着 IRQ、真被抢占过，而不是靠屏蔽混过去的。 */
                printk("[kern] user: exit ok code=%u preempt=%u\n",
                       (u32)f[0], (u32)(tick_count() - user_enter_ticks()));
                sched_set_l1((u64 *)0);
                pg_activate((u64 *)0);
                kthread_exit();
            }
            g_from_el0 = from_el0;
            {
                u64 r = syscall_dispatch(f[8], f[0], f[1], f[2]);
                f[0] = r; /* 返回值走 x0 */
            }
            g_from_el0 = 0;
        }
        /* 统计 EL1 自己的 svc（含 svc #0 这种立即数自检）：
         * EL0 的 puts/exit 不该污染 "svc handled, count=N" 这条数字。 */
        if (!from_el0) {
            svc_seen++;
        }
    } else if (ec == 0x24 || ec == 0x25) { /* data abort */
        /* 从 EL0 撞上来就没得“跳过指令”了：那等于让用户态静默越过保护。
         * 直接判这个线程死刑，并把 TTBR0 切回模板；内核自己的探测仍按老规矩
         * +4 继续，因为那是自检故意踩的。 */
        if ((f[33] & 0xFUL) == 0) {
            printk("[kern] user: fault esr=%x far=%x\n", esr, far);
            sched_set_l1((u64 *)0);
            pg_activate((u64 *)0);
            kthread_exit();
        }
        fault_seen++;
        f[32] += 4; /* 帧里 ELR 在 256 字节处 = f[32] */
    }

    printk("[exc] sync esr=%x elr=%x far=%x\n", esr, elr, far);

    if (ec != 0x15 && ec != 0x24 && ec != 0x25) {
        panic("unhandled synchronous exception");
    }
}

void do_fault(u64 *f)
{
    (void)f;
    panic("fiq/serror");
}

void do_irq(void)
{
    u32 iar = gicc[0x0C / 4];
    u32 id = iar & 0x3FF;

    if (id == 30) { /* 非安全物理定时器，PPI30 */
        ticks++;
        timer_arm();
    } else if (id == 33) { /* PL011 UART0 */
        uart_rx_isr();
    }
    if (id < 1020) {
        gicc[0x10 / 4] = iar;
    }
    sched_tick(); /* 最后一句：这里可能换栈，之后不再碰局部变量 */
}

/* ---- GICv2 ---- */
static void gic_enable(u32 id)
{
    gicd[(0x100 + (id / 32) * 4) / 4] = 1u << (id % 32);
    ((volatile u8 *)gicd)[0x400 + id] = 0x80;
    ((volatile u8 *)gicd)[0x800 + id] = 0x01;
}

static void gic_init(void)
{
    gicd[0] = 0; /* 先关，配完再开 */
    gic_enable(30);
    gic_enable(33);
    gicd[0] = 1; /* EnableGrp1 */
    gicc[0x04 / 4] = 0xF0; /* PMR */
    gicc[0] = 1; /* EnableGrp1 */
}

/* ---- 定时器 ---- */
static u64 period;

static void timer_arm(void)
{
    __asm__ volatile("msr cntp_tval_el0, %0" ::"r"(period));
}

static void timer_init(void)
{
    u64 frq;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    period = frq / 100; /* 100 Hz */
    timer_arm();
    __asm__ volatile("msr cntp_ctl_el0, %0" ::"r"(1UL));
}

/* ---- MMU：39-bit VA，开机模板在 paging.c ---- */
static void mmu_init(void)
{
    pg_init();

    __asm__ volatile("msr mair_el1, %0" ::"r"(0xFF00UL));
    __asm__ volatile("msr tcr_el1, %0" ::"r"(0x20000000003519UL));
    pg_activate((u64 *)0);

    {
        u64 s;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(s));
        s |= 1UL | 2UL | 4UL | (1UL << 12); /* M A C I */
        s &= ~(1UL << 22); /* 清 PAN：内核要替用户态读写缓冲区（copyin/out） */
        __asm__ volatile("msr sctlr_el1, %0; isb" ::"r"(s));
    }
}

/* ---- UART 接收 ---- */
#define RINGSZ 128

/* 串口线格式/FIFO 必须在“可能收字节”之前配好。
 * 写 LCR_H 的 FEN 位会复位收发 FIFO，放到开机末尾做，
 * 会把手快于内核的输入（管道里已经躺着的字节）直接冲掉。 */
static void uart_init(void)
{
    uart[0x2C / 4] = 0x70;      /* LCR_H: FEN + 8bit + 1stop + no parity */
    uart[0x34 / 4] = 0;         /* IFLS: FIFO 1/8 触发 */
    uart[UART_IMSC / 4] = (1u << 4); /* RXIM */
}
static volatile char ring[RINGSZ];
static volatile u32 rhead;
static volatile u32 rtail;

static void ring_push(char c)
{
    u32 n = (rhead + 1) % RINGSZ;

    if (n != rtail) {
        ring[rhead] = c;
        rhead = n;
    }
}

static int ring_pop(char *c)
{
    if (rtail == rhead) {
        return 0;
    }
    *c = ring[rtail];
    rtail = (rtail + 1) % RINGSZ;
    return 1;
}

static void uart_rx_isr(void)
{
    while (!(uart[UART_FR / 4] & FR_RXFE)) {
        ring_push((char)(uart[UART_DR / 4] & 0xFF));
    }
    uart[UART_ICR / 4] = 0x7FF;
}

/* ---- 命令 ---- */
static char cmd[64];
static u32 cmdlen;

static int streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void run_cmd(const char *s)
{
    if (s[0] == '\0') {
        return;
    }
    if (streq(s, "help")) {
        puts_("cmds: help ticks mmu exc fault\n");
    } else if (streq(s, "ticks")) {
        printk("ticks=%u\n", ticks);
    } else if (streq(s, "mmu")) {
        u64 sctlr, ttbr;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
        printk("sctlr=%x ttbr0=%x\n", sctlr, ttbr);
    } else if (streq(s, "exc")) {
        __asm__ volatile("svc #0x42");
        printk("svc handled, count=%u\n", svc_seen);
    } else if (streq(s, "fault")) {
        (void)*(volatile u64 *)0x80000000UL;
        printk("fault handled, count=%u\n", fault_seen);
    } else {
        printk("unknown: %s\n", s);
    }
}

static void feed(char c)
{
    if (c == '\r' || c == '\n') {
        uart_putc('\n');
        cmd[cmdlen] = '\0';
        run_cmd(cmd);
        cmdlen = 0;
        puts_("bare> ");
    } else if (c == 8 || c == 127) {
        if (cmdlen > 0) {
            cmdlen--;
            puts_("\b \b");
        }
    } else if (cmdlen < sizeof(cmd) - 1) {
        cmd[cmdlen++] = c;
        uart_putc(c);
    }
}

static u64 read_pct(void)
{
    u64 v;

    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

static int wait_ticks(u64 want)
{
    u64 frq;
    u64 start = read_pct();

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    while (ticks < want) {
        if (read_pct() - start > frq * 3) {
            return 0;
        }
        __asm__ volatile("wfi");
    }
    return 1;
}

extern char __stack_bottom[];
extern char __stack_top[];
extern void *kalloc(u64 n);
extern u64 heap_used(void);
extern u64 heap_size(void);

static volatile u64 bss_probe;

/* 两个内核线程：只证明“被抢占、被换回来、各自把话说完”。 */
static void thr_body(const char *tag)
{
    u64 i;

    for (i = 0; i < 4; i++) {
        u64 t0;

        __asm__ volatile("msr daifset, #2");
        printk("[thr] %s %u cur=%s\n", tag, i, kthread_cur());
        __asm__ volatile("msr daifclr, #2");
        t0 = tick_count();
        while (tick_count() < t0 + 8) {
            __asm__ volatile("wfi");
        }
    }
    __asm__ volatile("msr daifset, #2");
    printk("[thr] %s exit\n", tag);
    __asm__ volatile("msr daifclr, #2");
    kthread_exit();
}

static void thr_a(void *arg)
{
    thr_body((const char *)arg);
}

static void thr_b(void *arg)
{
    thr_body((const char *)arg);
}

int kmain(u64 dtb)
{
    u64 sp;
    u64 vbar;
    u64 sctlr;
    u32 magic = 0;

    puts_("\n[bare] aarch64 virt, PL011 UART up\n");
    uart_init(); /* 先配 FIFO/线格式，再谈收字节 */

    __asm__ volatile("mov %0, sp" : "=r"(sp));
    __asm__ volatile("mrs %0, currentel" : "=r"(sctlr));
    printk("[bare] el=%u  sp=%x\n", (sctlr >> 2) & 3, sp);
    printk("[bare] entry=%x  dtb=%x\n", (u64)(void *)kmain, dtb);

    if (dtb >= 0x40000000UL && dtb < 0x80000000UL) {
        magic = *(volatile u32 *)dtb;
    }
    printk("[bare] dtb magic=%x %s\n", (u64)magic, magic == 0xd00dfeedUL ? "ok" : "BAD");

    puts_("[bare] check: sp ");
    if (sp > (u64)__stack_bottom && sp <= (u64)__stack_top) {
        puts_("in-range ok\n");
    } else {
        puts_("OUT-OF-RANGE\n");
    }
    puts_("[bare] check: bss ");
    if (bss_probe == 0) {
        puts_("zeroed ok\n");
    } else {
        puts_("DIRTY\n");
    }
    bss_probe = 0xdead;

    __asm__ volatile("mrs %0, vbar_el1" : "=r"(vbar));
    printk("[bare] vec: vbar=%x armed\n", vbar);

    gic_init();
    timer_init();
    puts_("[bare] gic: ready\n");
    puts_("[bare] timer: 100 Hz\n");

    __asm__ volatile("msr daifclr, #2"); /* 开 IRQ */

    if (wait_ticks(5)) {
        printk("[bare] irq: ticks=%u ok\n", ticks);
    } else {
        printk("[bare] irq: TIMEOUT ticks=%u\n", ticks);
    }

    mmu_init();
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    printk("[bare] mmu: sctlr=%x %s\n", sctlr, (sctlr & 1UL) ? "on" : "OFF");
    printk("[bare] mmu: probe=%x (RAM 里读回)\n", (u64)(u64 *)&bss_probe);

    (void)*(volatile u64 *)0x80000000UL; /* 未映射，应当被抓到 */
    printk("[bare] fault: caught=%u %s\n", fault_seen, fault_seen > 0 ? "ok" : "MISS");

    __asm__ volatile("svc #0x42"); /* 同步异常：SVC */
    printk("[bare] svc: caught=%u %s\n", svc_seen, svc_seen > 0 ? "ok" : "MISS");

    /* S1：bump 堆，两块不重叠且 16B 对齐 */
    {
        void *a = kalloc(64);
        void *b = kalloc(100);
        if (a != (void *)0 && b != (void *)0 && a != b &&
            ((u64)a & 15UL) == 0 && ((u64)b & 15UL) == 0) {
            printk("[sys] heap ok used=%u/%u\n", heap_used(), heap_size());
        } else {
            puts_("[sys] heap FAIL\n");
        }
    }

    /* S0：走 svc #0x80 的写调用，验证门与返回值 */
    {
        const char *msg = "svc via 0x80\n";
        u64 r0;
        __asm__ volatile("mov x0, %1\n"
                         "mov x8, #1\n"
                         "svc #0x80\n"
                         "mov %0, x0"
                         : "=r"(r0)
                         : "r"(msg)
                         : "x0", "x8");
        puts_(r0 == 0 ? "[sys] syscall ok\n" : "[sys] syscall FAIL\n");
    }

    /* ---- 大内核：内存、VFS、调度都在同一个镜像、同一张页表里 ---- */
    puts_("[kern] init: mm\n");
    mm_init();
    puts_("[kern] init: vfs\n");
    vfs_init();
    puts_("[kern] init: sched\n");
    sched_init();
    puts_("[kern] init: done\n");
    printk("[kern] mm: pages=%u free=%u\n", mm_pages_total(), mm_pages_free());

    {
        void *p1 = kmalloc(24);
        void *p2 = kmalloc(200);
        if (p1 != (void *)0 && p2 != (void *)0 && p1 != p2) {
            kfree(p1);
            kfree(p2);
            printk("[kern] slab ok pages_free=%u\n", mm_pages_free());
        } else {
            puts_("[kern] slab FAIL\n");
        }
    }

    if (pg_selftest()) {
        printk("[kern] paging: 4K aspace ok free=%u\n", mm_pages_free());
    } else {
        puts_("[kern] paging FAIL\n");
    }

    {
        i64 fd = vfs_open("/etc/motd", 0);
        char b[64];
        i64 n = fd >= 0 ? vfs_read(fd, b, sizeof(b) - 1) : -1;

        if (n > 0) {
            b[n] = '\0';
            printk("[kern] vfs: %s", b);
            vfs_close(fd);
        } else {
            puts_("[kern] vfs FAIL\n");
        }
    }

    {
        char ls[128];
        i64 n = vfs_list("/", ls, sizeof(ls));

        if (n > 0) {
            printk("[kern] rootfs:\n%s", ls);
        } else {
            puts_("[kern] list FAIL\n");
        }
    }

    kthread_create("ta", thr_a, (void *)"A");
    kthread_create("tb", thr_b, (void *)"B");
    printk("[kern] sched: threads=%u cur=%s slice=5\n", kthread_alive(), kthread_cur());
    /* EL0 自检放在这条断言之后：threads 的数字不变，用户线程单独算一笔。 */
    if (kthread_create("u0", user_thread, (void *)0) < 0) {
        puts_("[kern] user: create thread failed\n");
    }
    /* 第二个用户线程故意越界，验证 EL0 的访问被拦、且只死它自己。 */
    if (kthread_create("u1", user_bad_thread, (void *)0) < 0) {
        puts_("[kern] user: create thread u1 failed\n");
    }
    /* 第三个：参数递内核地址，只看 syscall 层能不能挡住。 */
    if (kthread_create("u2", user_bad2_thread, (void *)0) < 0) {
        puts_("[kern] user: create thread u2 failed\n");
    }
    sched_go();
    schedule();
    while (kthread_alive() > 0) {
        schedule();
    }
    printk("[kern] sched: done cur=%s\n", kthread_cur());
    puts_("[kern] subsystems: mm sched vfs dev syscall\n");
    puts_("[kern] BIG_KERNEL_OK\n");

    puts_("[bare] uart: rx irq on\n");
    puts_("[bare] ALL_OK\n");
    puts_("bare> ");

    for (;;) {
        char c;
        /* 收字节只有一个主人：uart_rx_isr。
         * 主循环再去轮询 DR 会和中段抢 FIFO——某一边读到空 FIFO 就拿到 0x00，
         * 于是命令行里会冒出 "hel\0p" 这种脏字节。这里只消费环形缓冲。 */
        while (ring_pop(&c)) {
            feed(c);
        }
        __asm__ volatile("wfe");
    }
}