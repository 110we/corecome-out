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
#define ATTR_USER_RW (ATTR_NORM | ATTR_SH_IN | (1UL << 6))
#define ATTR_USER_RO (ATTR_NORM | ATTR_SH_IN | (1UL << 6) | (1UL << 7))
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
