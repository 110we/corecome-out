/* syscall.c —— 大内核的系统调用表。
 *
 * 约定不变：svc #0x80；x8 = 号；x0..x2 = 参数；x0 = 返回值。
 * 号 1 保留成旧的“打印字符串”，因为既有自检按它断言；
 * 真正的文件语义从 4 开始，走 VFS，不再直接摸 UART。
 *
 * 第三层权限就在这个文件：**用户指针一律校验**。
 * 前两层（页表 U 位/AP、EL0 取指与访存权限）拦得住“读写不该碰的页”，
 * 但拦不住“把内核地址当参数递进来”——内核对用户指针是直接解引用的，
 * EL1 读用户 VA 又是合法的。所以每个吃指针的号都必须先过 u_ok()：
 *   落在 [USER_WIN_BASE, USER_WIN_TOP)、长度不溢出、且当前 aspace 里真映射过。
 * 内核线程自己 svc 时不套这套（g_from_el0=0），它们传的是内核指针。
 */
#include "kern.h"

#define SYS_PUTS    1
#define SYS_TICKS   2
#define SYS_HEAP    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_READ    6
#define SYS_CLOSE   7
#define SYS_YIELD   8
#define SYS_EXIT    9
#define SYS_LIST   10

extern u64 heap_used(void);

/* 返回值 -2 = EFAULT：调用没通过参数校验，不是“功能没实现”。 */
#define EFAULT_  ((u64)-2)

static int u_ok(u64 p, u64 n)
{
    u64 *l1;

    if (n == 0) {
        return 1;
    }
    if (p < USER_WIN_BASE || p >= USER_WIN_TOP) {
        return 0;
    }
    if (p + n < p || p + n > USER_WIN_TOP) { /* 长度回绕 */
        return 0;
    }
    l1 = sched_get_l1();
    if (l1 == (u64 *)0) {
        return 0; /* 没有 aspace 就不该有用户指针 */
    }
    /* 逐页看：首尾都映射不代表中间没有洞 */
    while (p < p + n && n > 0) {
        u64 step = 4096 - (p & 4095);
        if (pg_user_pa(l1, p) == 0) {
            return 0;
        }
        if (step > n) {
            break;
        }
        p += step;
        n -= step;
    }
    return 1;
}

/* 逐页校验的 copyin：字符串可能贴着页尾、下一页未必映射，
 * 直接 kputs() 用户指针等于让内核替用户跑野指针。 */
static int u_str(u64 p, char *dst, u64 cap)
{
    u64 i;

    for (i = 0; i + 1 < cap; i++) {
        if (!u_ok(p + i, 1)) {
            return -1;
        }
        dst[i] = *(const char *)(p + i);
        if (dst[i] == 0) {
            return (int)i;
        }
    }
    dst[cap - 1] = 0;
    return (int)(cap - 1);
}

static u64 deny(u64 nr, u64 p)
{
    printk("[kern] user: syscall denied nr=%u ptr=%x\n", (u32)nr, (u32)p);
    return EFAULT_;
}

u64 syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2)
{
    switch (nr) {
    case SYS_PUTS:
        if (g_from_el0) {
            char buf[128];
            if (u_str(a0, buf, sizeof(buf)) < 0) {
                return deny(nr, a0);
            }
            kputs(buf);
            return 0;
        }
        kputs((const char *)a0);
        return 0;
    case SYS_TICKS:
        return tick_count();
    case SYS_HEAP:
        return heap_used();
    case SYS_WRITE:
        if (g_from_el0 && !u_ok(a1, a2)) {
            return deny(nr, a1);
        }
        return (u64)vfs_write((int)a0, (const void *)a1, a2);
    case SYS_OPEN:
        if (g_from_el0) {
            char path[VFS_PATH_MAX];
            if (u_str(a0, path, sizeof(path)) < 0) {
                return deny(nr, a0);
            }
            return (u64)vfs_open(path, (int)a1);
        }
        return (u64)vfs_open((const char *)a0, (int)a1);
    case SYS_READ:
        if (g_from_el0 && !u_ok(a1, a2)) {
            return deny(nr, a1);
        }
        return (u64)vfs_read((int)a0, (void *)a1, a2);
    case SYS_CLOSE:
        return (u64)vfs_close((int)a0);
    case SYS_YIELD:
        schedule();
        return 0;
    case SYS_EXIT:
        kthread_exit();
        return 0; /* 不会到这里 */
    case SYS_LIST:
        if (g_from_el0 && (!u_ok(a0, 1) || !u_ok(a1, a2))) {
            return deny(nr, a0);
        }
        return (u64)vfs_list((const char *)a0, (char *)a1, a2);
    default:
        return (u64)-1; /* ENOSYS */
    }
}