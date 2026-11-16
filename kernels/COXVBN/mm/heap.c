/* heap.c —— S1：bump 分配器。
 * 没有 free：先证明“能要一块内存、地址对齐、不重叠”，
 * 后面换 slab 时这层的接口不变。
 */

typedef unsigned long u64;

#define HEAP_SIZE (64UL * 1024UL)

static unsigned char heap[HEAP_SIZE] __attribute__((aligned(16)));
static u64 used;

void *kalloc(u64 n)
{
    u64 a = (used + 15UL) & ~15UL;

    if (n == 0 || a + n > HEAP_SIZE) {
        return (void *)0;
    }
    used = a + n;
    return (void *)&heap[a];
}

void bump_free(void *p)
{
    (void)p; /* bump：暂时什么也不做 */
}

u64 heap_used(void)
{
    return used;
}

u64 heap_size(void)
{
    return HEAP_SIZE;
}