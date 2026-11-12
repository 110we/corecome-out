/* paging.c —— 4K 粒度页表 + 用户地址空间。
 *
 * 原来 1GB block 恒等映射够内核自己用，但 EL0 需要“同一把 VA、
 * 不同物理页”的隔离，block 映射连颗粒度都不给。这里做三件事：
 *
 *   1. 开机模板：L1[0] -> L2（0..1GB，设备块），L1[1] -> 内核块；
 *   2. 每个进程一份 aspace = 拷一份 L1/L2 + 一张 L3（管 2MB 用户窗）；
 *   3. 切进程只换 TTBR0 + 刷 TLB，内核镜像在每个 aspace 里都可见。
 *
 * 用户窗固定 0x10000000..0x101FFFFF：一个 L3 正好管完，省掉多级查找。
 */
#include "kern.h"

#define PG_SIZE   4096UL
#define IDX_MASK  0x1FFUL
#define PA_MASK   0x0000FFFFFFFFF000UL

/* 描述符属性：AttrIdx0=Normal(WB)，AttrIdx1=Device(nGnRnE)，见 kernel.c 的 MAIR.
 * 低两位是描述符类型，不能混用：
 *   L1/L2 的 block = 0b01，L1/L2 的 table = 0b11，L3 的 page = 0b11。
 * 把 block 写成 0b11，CPU 就会把内核基址当二级表去走，一取指就飞。 */
#define ATTR_BLOCK   0x1UL
#define ATTR_TABLE   0x3UL
#define ATTR_PAGE    0x3UL
#define ATTR_AF      (1UL << 10)
#define ATTR_DEV     (ATTR_BLOCK | (1UL << 2) | ATTR_AF | (1UL << 53) | (1UL << 54))
#define ATTR_NORM    (ATTR_PAGE | ATTR_AF)
#define ATTR_NORM_B  (ATTR_BLOCK | ATTR_AF)
#define ATTR_SH_IN   (3UL << 8)   /* inner shareable */

/* 用户页属性：AArch64 的 AP 只有两位——
 *   bit6 = AP[1]：置 1 才允许 EL0 访问；
 *   bit7 = AP[2]：置 1 是只读。
 * 之前把 bit7 当“用户位”用，结果页是 EL1 只读，写立即 permission fault。
 * bit[9:8] 才是 SH，别和 AP 混。 */
/* bit6 = AP[1]：EL0 可访问；bit7 = AP[2]：只读；
 * bit54 = UXN：EL0 不可执行。数据页必须显式挂 UXN，
 * 否则用户栈、用户数据就是一块 RWX。 */
#define UXN          (1UL << 54)
#define ATTR_USER_RW (ATTR_NORM | ATTR_SH_IN | (1UL << 6) | UXN)
#define ATTR_USER_RO (ATTR_NORM | ATTR_SH_IN | (1UL << 6) | (1UL << 7) | UXN)
#define ATTR_USER_RX (ATTR_NORM | ATTR_SH_IN | (1UL << 6) | (1UL << 7))

static u64 boot_l1[512] __attribute__((aligned(PG_SIZE)));
static u64 boot_l2[512] __attribute__((aligned(PG_SIZE)));

/* 用户窗在 L2 里的下标：0x10000000 / 2MB = 128 */
#define WIN_L2_IDX  128UL

u64 *pg_template(void)
{
    return boot_l1;
}

void pg_init(void)
{
    u64 i;

    for (i = 0; i < 512; i++) {
        boot_l1[i] = 0;
        boot_l2[i] = 0;
    }

    /* 0x00000000-0x0A1FFFFF 走设备属性：UART0 在 0x09000000（L2 索引 72），
     * GIC 在 0x08000000（索引 64）。只映前 10MB 的话，MMU 一开 UART 就没了。 */
    boot_l1[0] = (u64)boot_l2 | ATTR_TABLE;
    for (i = 0; i <= 80; i++) {
        boot_l2[i] = (i * 0x200000UL) | ATTR_DEV;
    }

    /* 0x40000000-0x7FFFFFFF：内核 Normal 可执行（block，不是 table） */
    boot_l1[1] = 0x40000000UL | ATTR_NORM_B;
}

/* 新地址空间：L1/L2 照模板抄一份，L3 清零待填。 */
static u64 *last_aspace;   /* kstruct 用：最近派生的一份 aspace */

u64 *pg_aspace_new(void)
{
    u64 *l1 = (u64 *)mm_page_alloc();
    u64 *l2;
    u64 *l3;
    u64 i;

    if (l1 == (u64 *)0) {
        return (u64 *)0;
    }
    l2 = (u64 *)mm_page_alloc();
    if (l2 == (u64 *)0) {
        return (u64 *)0;
    }
    l3 = (u64 *)mm_page_alloc();
    if (l3 == (u64 *)0) {
        return (u64 *)0;
    }

    for (i = 0; i < 512; i++) {
        l1[i] = boot_l1[i];
        l2[i] = boot_l2[i];
    }
    l1[0] = (u64)l2 | ATTR_TABLE;
    l2[WIN_L2_IDX] = (u64)l3 | ATTR_TABLE;

    for (i = 0; i < 512; i++) {
        l3[i] = 0;
    }
    last_aspace = l1;
    pg_aspace_ref(l1);   /* 出生即持有 1 次引用 */
    return l1;
}

static u64 *win_l3(u64 *l1)
{
    u64 *l2;

    if (l1 == (u64 *)0) {
        return (u64 *)0;
    }
    l2 = (u64 *)(l1[0] & PA_MASK);
    if (l2 == (u64 *)0) {
        return (u64 *)0;
    }
    return (u64 *)(l2[WIN_L2_IDX] & PA_MASK);
}

/* va 必须落在用户窗内，否则返回 -1（越界不许静默成功）。
 * attr 收的是 kern.h 的权限等级，翻译成描述符位。 */
int pg_map_user(u64 *l1, u64 va, u64 pa, u64 attr)
{
    u64 *l3 = win_l3(l1);
    u64 bits;
    u64 idx;

    if (l3 == (u64 *)0 || va < 0x10000000UL || va >= 0x10200000UL) {
        return -1;
    }
    if ((va & (PG_SIZE - 1)) != 0 || (pa & (PG_SIZE - 1)) != 0) {
        return -1;
    }
    switch (attr) {
    case PG_USER_RW:
        bits = ATTR_USER_RW;
        break;
    case PG_USER_RO:
        bits = ATTR_USER_RO;
        break;
    case PG_USER_RX:
        bits = ATTR_USER_RX;
        break;
    default:
        return -1;
    }
    idx = (va >> 12) & IDX_MASK;
    l3[idx] = (pa & PA_MASK) | bits;
    return 0;
}

u64 pg_user_pa(u64 *l1, u64 va)
{
    u64 *l3 = win_l3(l1);
    u64 e;

    if (l3 == (u64 *)0 || va < 0x10000000UL || va >= 0x10200000UL) {
        return 0;
    }
    e = l3[(va >> 12) & IDX_MASK];
    if ((e & 1UL) == 0) {
        return 0;
    }
    return (e & PA_MASK) | (va & (PG_SIZE - 1));
}

/* 切地址空间：内核在每个 aspace 里都恒等映射，所以写完 TTBR0 还能继续跑。 */
void pg_activate(u64 *l1)
{
    u64 ttbr = (l1 == (u64 *)0) ? (u64)boot_l1 : (u64)l1;

    __asm__ volatile("msr ttbr0_el1, %0" ::"r"(ttbr));
    __asm__ volatile("dsb sy; isb");
    __asm__ volatile("tlbi vmalle1; dsb sy; isb");
}

/* 自检：开一个 aspace，映射一页，激活后从新 VA 写读，再切回模板。
 * 这一步过了才谈得上 EL0 —— 页表要是错，EL0 只会给你一串 fault。 */
int pg_selftest(void)
{
    u64 *l1 = pg_aspace_new();
    u8 *page = (u8 *)mm_page_alloc();
    u64 i;
    int ok = 1;

    if (l1 == (u64 *)0 || page == (u8 *)0) {
        return 0;
    }
    for (i = 0; i < PG_SIZE; i++) {
        page[i] = (u8)(i & 0xFF);
    }
    if (pg_map_user(l1, 0x10000000UL, (u64)page, PG_USER_RW) != 0) {
        return 0;
    }
    if (pg_user_pa(l1, 0x10000000UL) != (u64)page) {
        ok = 0;
    }

    pg_activate(l1);
    for (i = 0; i < PG_SIZE; i += 97) {
        if (*(volatile u8 *)(0x10000000UL + i) != (u8)(i & 0xFF)) {
            ok = 0;
            break;
        }
    }
    *(volatile u64 *)0x10000FF8UL = 0x5A5AA5A5UL;
    pg_activate((u64 *)0);

    if (*(volatile u64 *)(page + PG_SIZE - 8) != 0x5A5AA5A5UL) {
        ok = 0;
    }
    return ok;
}

/* ---- 写时复制（COW）----
 * fork 不再复制内容：父子共享同一批物理页，两边都降成只读，
 * 页上打一个 cow 标记、引用计数 +1。谁先写谁吃 permission fault，
 * 由 pg_cow_fault() 复制一页给他，另一个人的内容原样不动。
 * 代码页（RX）本来就不可写，直接共享，连“复制”这一步都省掉。
 */
static u64 cow_faults;
static u64 cow_copies;
static u64 cow_shared;

static void tlb_flush_all(void)
{
    __asm__ volatile("dsb sy; tlbi vmalle1; dsb sy; isb");
}

static void tlb_flush_va(u64 va)
{
    u64 op = va >> 12;

    __asm__ volatile("dsb sy; tlbi vaae1, %0; dsb sy; isb" ::"r"(op));
}

u64 *pg_aspace_clone(u64 *src)
{
    u64 *dst = pg_aspace_new();
    u64 *ls;
    u64 *ld;
    u64 i;

    if (dst == (u64 *)0) {
        return (u64 *)0;
    }
    ls = win_l3(src);
    ld = win_l3(dst);
    if (ls == (u64 *)0 || ld == (u64 *)0) {
        return dst; /* 父进程没有用户窗，空壳即可 */
    }
    for (i = 0; i < 512; i++) {
        u64 e = ls[i];
        u64 pa;
        u64 va;

        if ((e & 1UL) == 0) {
            continue;
        }
        pa = e & PA_MASK;
        va = USER_WIN_BASE + i * 4096;

        if ((e & UXN) == 0) {          /* 代码页：只读可执行，共享 */
            mm_page_ref((void *)pa);
            if (pg_map_user(dst, va, pa, PG_USER_RX) != 0) {
                return (u64 *)0;
            }
            cow_shared++;
            continue;
        }
        /* 数据页：父子的 PTE 都改成只读，标记 COW，等写故障再分家 */
        ls[i] = pa | ATTR_USER_RO;
        mm_page_set_cow((void *)pa, 1);
        mm_page_ref((void *)pa);
        if (pg_map_user(dst, va, pa, PG_USER_RO) != 0) {
            return (u64 *)0;
        }
        cow_shared++;
    }
    /* 父进程的 PTE 刚被降成只读，TLB 里还留着可写的旧项 —— 必须刷掉，
     * 否则父进程下一次写会绕开保护，直接改到孩子脸上的共享页。 */
    tlb_flush_all();
    return dst;
}

/* 写故障入口：只在“用户窗 + 只读 + cow 标记”三个条件同时成立时接管。
 * 返回 0 = 复制完成，调用者必须重试原指令（不能推进 ELR）。
 * 返回 -1 = 不是 COW（真只读 / 不在窗里 / 没页），交给调用者杀进程。 */
int pg_cow_fault(u64 far)
{
    u64 va = far & ~0xFFFUL;
    u64 *l1 = sched_get_l1();
    u64 *l3;
    u64 idx;
    u64 e;
    u64 pa;
    u8 *np;
    u64 i;

    if (l1 == (u64 *)0) {
        return -1;
    }
    l3 = win_l3(l1);
    if (l3 == (u64 *)0 || va < USER_WIN_BASE || va >= USER_WIN_TOP) {
        return -1;
    }
    idx = (va >> 12) & IDX_MASK;
    e = l3[idx];
    if ((e & 1UL) == 0) {
        return -1;                 /* 没映射：越界，不是 COW */
    }
    if ((e & (1UL << 7)) == 0) {
        return -1;                 /* 本来就可写：这不是我们的故障 */
    }
    pa = e & PA_MASK;
    if (mm_page_cow((void *)pa) == 0) {
        return -1;                 /* 真只读（代码/只读数据）→ 杀进程 */
    }

    np = (u8 *)mm_page_alloc();
    if (np == (u8 *)0) {
        return -1;
    }
    for (i = 0; i < 4096; i++) {
        np[i] = ((u8 *)pa)[i];
    }
    mm_page_set_cow((void *)np, 0);
    mm_page_unref((void *)pa);      /* 让出旧页的一份引用 */
    l3[idx] = (u64)np | ATTR_USER_RW;
    tlb_flush_va(va);

    cow_faults++;
    cow_copies++;
    return 0;
}

u64 pg_cow_faults(void)
{
    return cow_faults;
}

void pg_cow_stats(u64 *copies, u64 *shared)
{
    *copies = cow_copies;
    *shared = cow_shared;
}

/* ---- 地址空间引用计数 ----
 * fork 出来的进程各占一份；clone(CLONE_VM) 的线程共用同一份，
 * 只加引用。最后一个使用者（被 wait 收尸时）才真正拆页表、还用户页。
 * 不做这张表的话，线程一退就把父进程的 aspace 拆了 —— 那是必崩的写法。
 */
#define ASP_MAX 64

static struct {
    u64 *l1;
    u32 refs;
} asp[ASP_MAX];

static int asp_slot(u64 *l1, int create)
{
    int i;
    int free_slot = -1;

    for (i = 0; i < ASP_MAX; i++) {
        if (l1 != (u64 *)0 && asp[i].l1 == l1) {
            return i;
        }
        if (asp[i].l1 == (u64 *)0 && free_slot < 0) {
            free_slot = i;
        }
    }
    return create ? free_slot : -1;
}

void pg_aspace_ref(u64 *l1)
{
    int s;

    if (l1 == (u64 *)0) {
        return;
    }
    s = asp_slot(l1, 1);
    if (s < 0) {
        printk("[paging] aspace table full\n");
        return;
    }
    asp[s].l1 = l1;
    asp[s].refs++;
}

void pg_aspace_unref(u64 *l1)
{
    u64 *l2;
    u64 *l3;
    u64 i;
    int s;

    if (l1 == (u64 *)0) {
        return;
    }
    s = asp_slot(l1, 0);
    if (s < 0) {
        return; /* 不是登记过的 aspace：不动它，宁漏不拆 */
    }
    if (asp[s].refs > 1) {
        asp[s].refs--;
        return;
    }
    asp[s].l1 = (u64 *)0;
    asp[s].refs = 0;

    l3 = win_l3(l1);
    if (l3 != (u64 *)0) {
        for (i = 0; i < 512; i++) {
            u64 e = l3[i];

            if ((e & 1UL) != 0) {
                mm_page_unref((void *)(e & PA_MASK)); /* 共享页只还引用 */
            }
        }
        mm_page_free(l3);
    }
    l2 = (u64 *)(l1[0] & PA_MASK);
    if (l2 != (u64 *)0) {
        mm_page_free(l2);
    }
    mm_page_free(l1);
}

/* 给 kstruct 看的解码：用户窗里到底映射了什么、什么权限 */
void pg_dump(u64 *l1)
{
    u64 *l3;
    u64 i;

    if (l1 == (u64 *)0) {
        l1 = pg_template();
        printk("[paging] cur=template (内核线程无私有 aspace)\n");
        if (win_l3(l1) == (u64 *)0 && last_aspace != (u64 *)0) {
            l1 = last_aspace;
            printk("[paging] 模板没有用户窗，改看最近派生的 aspace\n");
        }
    }
    l3 = win_l3(l1);
    if (l3 == (u64 *)0) {
        printk("[paging] 该 aspace 未映射用户窗\n");
        return;
    }
    printk("[paging] ttbr0=%x win=[%x,%x) 4K\n", (u32)(u64)l1,
           (u32)USER_WIN_BASE, (u32)USER_WIN_TOP);
    for (i = 0; i < 512; i++) {
        u64 e = l3[i];
        const char *kind;

        if ((e & 1UL) == 0) {
            continue;
        }
        if ((e & UXN) != 0) {
            kind = (e & (1UL << 7)) != 0 ? "RO" : "RW";
        } else {
            kind = "RX";
        }
        printk("  %x -> %x %s U=1\n", (u32)(USER_WIN_BASE + i * 4096),
               (u32)(e & PA_MASK), kind);
    }
}
