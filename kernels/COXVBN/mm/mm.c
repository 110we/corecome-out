/* mm.c —— 页分配 + slab。大内核里所有对象都从这里出。
 *
 * 页：512 KiB 静态池，128 个 4K 页，位图管理。
 * slab：7 个尺寸档，每块前面 16 字节头（next + meta），free 可复用。
 * 先不做页回收：块回了 slab，页不还池子——够跑，且能看出泄漏。
 */
#include "kern.h"

#define PAGE_SIZE   4096UL
#define POOL_PAGES  128UL
#define MAGIC       0xA5A5A5A5UL
#define NCLASS      7

static u8 pool[POOL_PAGES * PAGE_SIZE] __attribute__((aligned(4096)));
static u64 bitmap[POOL_PAGES / 64];
/* 引用计数与 COW 标记：按页号索引，跟位图同生共死。
 * refs==0 才回池子；cowf 表示“这页是 fork 时改成只读的”，
 * 两者一起才能把“写时复制”和“本来就只读（代码页）”分开。 */
static u16 refs[POOL_PAGES];
static u8 cowf[POOL_PAGES];
static const u64 cls_size[NCLASS] = {16, 32, 64, 128, 256, 512, 1024};
static void *freelist[NCLASS];

typedef struct blk {
    struct blk *next;
    u64 meta;               /* (MAGIC << 8) | class */
} blk_t;

static void *page_alloc(void)
{
    u64 i;
    for (i = 0; i < POOL_PAGES; i++) {
        if ((bitmap[i / 64] & (1UL << (i % 64))) == 0) {
            bitmap[i / 64] |= (1UL << (i % 64));
            refs[i] = 1;
            cowf[i] = 0;
            return (void *)&pool[i * PAGE_SIZE];
        }
    }
    return (void *)0;
}

/* 页号换算：不是池里的页返回 -1（外部指针不许改位图/计数） */
static int page_idx(void *p)
{
    u8 *q = (u8 *)p;

    if (q < &pool[0] || q >= &pool[POOL_PAGES * PAGE_SIZE]) {
        return -1;
    }
    if (((u64)(q - &pool[0]) & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    return (int)((u64)(q - &pool[0]) / PAGE_SIZE);
}

void mm_init(void)
{
    u64 i;
    for (i = 0; i < POOL_PAGES / 64; i++) {
        bitmap[i] = 0;
    }
    for (i = 0; i < POOL_PAGES; i++) {
        refs[i] = 0;
        cowf[i] = 0;
    }
    for (i = 0; i < NCLASS; i++) {
        freelist[i] = (void *)0;
    }
}

/* 整页出口：页表要的是“干净的一页”，拿旧数据去填 L3 等于埋雷。 */
void *mm_page_alloc(void)
{
    u8 *p = (u8 *)page_alloc();
    u64 i;

    if (p == (u8 *)0) {
        return (void *)0;
    }
    for (i = 0; i < PAGE_SIZE; i++) {
        p[i] = 0;
    }
    return (void *)p;
}

void mm_page_free(void *p)
{
    int i = page_idx(p);

    if (i < 0) {
        return; /* 不是池里的页，宁可泄漏也不改位图 */
    }
    bitmap[i / 64] &= ~(1UL << (i % 64));
    refs[i] = 0;
    cowf[i] = 0;
}

/* ---- 引用计数：COW 的账本 ---- */
void mm_page_ref(void *p)
{
    int i = page_idx(p);

    if (i >= 0 && refs[i] < 0xFFFF) {
        refs[i]++;
    }
}

/* 归还一次引用；返回剩余引用数。归零才真正还池子。 */
int mm_page_unref(void *p)
{
    int i = page_idx(p);

    if (i < 0) {
        return -1;
    }
    if (refs[i] > 1) {
        refs[i]--;
        return (int)refs[i];
    }
    refs[i] = 0;
    bitmap[i / 64] &= ~(1UL << (i % 64));
    cowf[i] = 0;
    return 0;
}

u64 mm_page_refcnt(void *p)
{
    int i = page_idx(p);

    return i < 0 ? 0 : (u64)refs[i];
}

void mm_page_set_cow(void *p, int on)
{
    int i = page_idx(p);

    if (i >= 0) {
        cowf[i] = on ? 1 : 0;
    }
}

int mm_page_cow(void *p)
{
    int i = page_idx(p);

    return i < 0 ? 0 : (int)cowf[i];
}

u64 mm_pages_total(void)
{
    return POOL_PAGES;
}

u64 mm_pages_free(void)
{
    u64 i, n = 0;
    for (i = 0; i < POOL_PAGES; i++) {
        if ((bitmap[i / 64] & (1UL << (i % 64))) == 0) {
            n++;
        }
    }
    return n;
}

void *kmalloc(u64 n)
{
    u64 ci;
    blk_t *b;

    if (n == 0 || n > cls_size[NCLASS - 1]) {
        return (void *)0;
    }
    for (ci = 0; ci < NCLASS; ci++) {
        if (cls_size[ci] >= n) {
            break;
        }
    }

    b = (blk_t *)freelist[ci];
    if (b != (blk_t *)0) {
        freelist[ci] = b->next;
    } else {
        u64 step = cls_size[ci] + sizeof(blk_t);
        u64 off;
        u8 *pg = (u8 *)page_alloc();

        if (pg == (u8 *)0) {
            return (void *)0;
        }
        for (off = 0; off + step <= PAGE_SIZE; off += step) {
            blk_t *nb = (blk_t *)(pg + off);
            nb->meta = (MAGIC << 8) | ci;
            nb->next = (blk_t *)freelist[ci];
            freelist[ci] = nb;
        }
        b = (blk_t *)freelist[ci];
        freelist[ci] = b->next;
    }
    return (void *)((u8 *)b + sizeof(blk_t));
}

void kfree(void *p)
{
    blk_t *b;
    u64 cls;

    if (p == (void *)0) {
        return;
    }
    b = (blk_t *)((u8 *)p - sizeof(blk_t));
    if ((b->meta >> 8) != MAGIC) {
        return; /* 不是这里面的块，宁可泄漏也不写坏 */
    }
    cls = b->meta & 0xFFUL;
    if (cls >= NCLASS) {
        return;
    }
    b->next = (blk_t *)freelist[cls];
    freelist[cls] = b;
}