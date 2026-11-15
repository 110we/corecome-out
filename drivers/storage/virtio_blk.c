/* virtio_blk.c —— QEMU virt 机上的 virtio-mmio 块设备（legacy）。
 *
 * 只做 legacy（version 1）：virt 机的 virtio-mmio 默认就是它。
 * 队列一页放描述符表 + avail，used 环按 QueueAlign 落在下一页，
 * 所以那两页必须连号。
 * 缓冲区全是内核静态数组，物理地址 == 虚拟地址（恒等映射），
 * 描述符里直接写指针。真机还要管内核 cache 一致性（DMA 前后
 * clean/invalidate），这里只保证 dsb 的写序。
 */
#include "kern.h"
#include "blk.h"

#define VIRT_BASE   0x0A000000UL
#define VIRT_STRIDE 0x200UL
#define VIRT_SLOTS  32   /* virt 机一共 32 个 mmio 传输口，设备从高位往低位插 */

/* legacy（version=1）的寄存器表比 modern 整体偏 8 字节：
 * 0x028 是 GuestPageSize，不是 QueueSel。照 modern 的表写，
 * 所有配置都落在错误寄存器上，表现就是“写进去读回来全是 0”。 */
#define R_MAGIC      0x000
#define R_DEVID      0x008
#define R_GUESTFEAT  0x020
#define R_GUESTPAGE  0x028
#define R_QUEUESEL   0x030
#define R_QUEUENUMMAX 0x034
#define R_QUEUENUM   0x038
#define R_QUEUEALIGN 0x03C
#define R_QUEUEPFN   0x040
#define R_QREADY     0x044   /* modern 用；legacy 写不写都行 */
#define R_QNOTIFY    0x050
#define R_ISTATUS    0x060
#define R_IACK       0x064
#define R_STATUS     0x070
#define R_CONFIG     0x100   /* legacy：设备配置从 0x100 起（modern 才是 0x50） */

#define ST_ACK      1
#define ST_DRIVER   2
#define ST_FEATOK   8
#define ST_DRIVEROK 4

#define VQ_SIZE 8
#define D_NEXT  1
#define D_WRITE 2

typedef struct vdesc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} vdesc_t;

typedef struct vavail {
    u16 flags;
    u16 idx;
    u16 ring[VQ_SIZE];
} vavail_t;

typedef struct vused_elem {
    u32 id;
    u32 len;
} vused_elem_t;

typedef struct vused {
    u16 flags;
    u16 idx;
    vused_elem_t ring[VQ_SIZE];
} vused_t;

static u8 queue_mem[VQ_SIZE * 16 + 64 + 4096] __attribute__((aligned(4096)));
#define QDESC  ((volatile vdesc_t *)queue_mem)
#define QAVAIL ((volatile vavail_t *)(queue_mem + VQ_SIZE * 16))
#define QUSED  ((volatile vused_t *)(queue_mem + 4096))

static volatile u32 *regs;
static u16 used_seen;
static u64 req_count;
static u64 dev_sectors;

static u8 req_page[4096] __attribute__((aligned(4096)));
static volatile u8 req_status __attribute__((aligned(16)));
static u8 payload[4096] __attribute__((aligned(4096)));

static u32 rd(u32 off)
{
    return regs[off / 4];
}

static void wr(u32 off, u32 v)
{
    regs[off / 4] = v;
    /* memory clobber 不能省：否则编译器可以把环里的写挪到通知之后 */
    __asm__ volatile("dsb sy" ::: "memory");
}

u64 blk_sectors(void)
{
    return dev_sectors;
}

u64 blk_reqs(void)
{
    return req_count;
}

int blk_init(void)
{
    u32 i;

    for (i = 0; i < VIRT_SLOTS; i++) {
        volatile u32 *r = (volatile u32 *)(VIRT_BASE + i * VIRT_STRIDE);

        if (r[R_MAGIC / 4] != 0x74726976UL) { /* "virt" */
            continue;
        }
        if (r[R_DEVID / 4] != 2) {            /* 2 = block */
            continue;
        }
        printk("[blk] found virtio-mmio slot%u\n", i);
        regs = r;
        break;
    }
    if (regs == (volatile u32 *)0) {
        return -1;
    }

    /* 复位 → 认驱动 → 谈特性。特性一位都不要：纯轮询、无中断、无 RO 位。 */
    wr(R_GUESTPAGE, 4096);
    wr(R_STATUS, 0);
    wr(R_STATUS, ST_ACK);
    wr(R_STATUS, ST_ACK | ST_DRIVER);
    wr(R_GUESTFEAT, 0);
    /* legacy 没有 FEATURES_OK 这一步：ACK|DRIVER 之后就配队列 */
    wr(R_STATUS, ST_ACK | ST_DRIVER);

    wr(R_QUEUESEL, 0);
    if (rd(R_QUEUENUMMAX) < VQ_SIZE) {
        return -1;
    }
    wr(R_QUEUENUM, VQ_SIZE);
    wr(R_QUEUEALIGN, 4096);
    wr(R_QUEUEPFN, (u32)((u64)queue_mem >> 12));

    for (i = 0; i < VQ_SIZE; i++) {
        QDESC[i].addr = 0;
        QDESC[i].len = 0;
        QDESC[i].flags = 0;
        QDESC[i].next = 0;
    }
    QAVAIL->flags = 0;
    QAVAIL->idx = 0;
    QUSED->flags = 0;
    QUSED->idx = 0;
    used_seen = 0;
    wr(R_IACK, 0xFFFFFFFFUL);
    wr(R_STATUS, ST_ACK | ST_DRIVER | ST_DRIVEROK);

    /* 配置区头 8 字节 = 容量（512B 扇区数） */
    dev_sectors = (u64)rd(R_CONFIG) | ((u64)rd(R_CONFIG + 4) << 32);
    req_count = 0;
    if (rd(R_STATUS) != (ST_ACK | ST_DRIVER | ST_DRIVEROK)) {
        return -1;
    }
    return 0;
}
/* 一次同步读：三描述符链（头 / 数据 / 状态），轮询 used.idx。
 * 没有中断、没有回收队列——一次一个请求，简单到不会错。 */
int blk_read(u64 sector, void *buf, u32 nsec)
{
    u32 len = nsec * 512;
    u64 frq;
    u64 start;
    u64 limit;
    u32 i;

    if (regs == (volatile u32 *)0 || len == 0 || len > sizeof(payload)) {
        return -1;
    }
    ((u32 *)req_page)[0] = 0;      /* VIRTIO_BLK_T_IN */
    ((u32 *)req_page)[1] = 0;
    ((u64 *)req_page)[1] = sector;
    req_status = 0xFF;

    QDESC[0].addr = (u64)req_page;
    QDESC[0].len = 16;
    QDESC[0].flags = D_NEXT;
    QDESC[0].next = 1;

    QDESC[1].addr = (u64)payload;
    QDESC[1].len = len;
    QDESC[1].flags = D_NEXT | D_WRITE;
    QDESC[1].next = 2;

    QDESC[2].addr = (u64)&req_status;
    QDESC[2].len = 1;
    QDESC[2].flags = D_WRITE;
    QDESC[2].next = 0;

    QAVAIL->ring[QAVAIL->idx % VQ_SIZE] = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    QAVAIL->idx++;
    __asm__ volatile("dsb sy" ::: "memory");
    wr(R_QNOTIFY, 0);

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(start));
    limit = frq / 20;              /* 50 ms：一次 4K DMA 的上限 */
    while (QUSED->idx == used_seen) {
        u64 now;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
        if (now - start > limit) {
            printk("[blk] timeout req=%u sector=%u\n", (u32)req_count,
                   (u32)sector);
            return -1;
        }
    }
    used_seen = QUSED->idx;
    req_count++;
    __asm__ volatile("dmb sy");

    if (req_status != 0) {
        printk("[blk] bad status=%u sector=%u\n", (u32)req_status, (u32)sector);
        return -1;
    }
    for (i = 0; i < len; i++) {
        ((u8 *)buf)[i] = payload[i];
    }
    return 0;
}