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
            return (void *)&pool[i * PAGE_SIZE];
        }
    }
    return (void *)0;
}

void mm_init(void)
{
    u64 i;
    for (i = 0; i < POOL_PAGES / 64; i++) {
        bitmap[i] = 0;
    }
    for (i = 0; i < NCLASS; i++) {
        freelist[i] = (void *)0;
    }
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