/* syscall.c —— 大内核的系统调用表。
 *
 * 约定不变：svc #0x80；x8 = 号；x0..x2 = 参数；x0 = 返回值。
 * 号 1 保留成旧的“打印字符串”，因为既有自检按它断言；
 * 真正的文件语义从 4 开始，走 VFS，不再直接摸 UART。
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

extern u64 tick_count(void);
extern u64 heap_used(void);

u64 syscall_dispatch(u64 nr, u64 a0, u64 a1, u64 a2)
{
    switch (nr) {
    case SYS_PUTS:
        kputs((const char *)a0);
        return 0;
    case SYS_TICKS:
        return tick_count();
    case SYS_HEAP:
        return heap_used();
    case SYS_WRITE:
        return (u64)vfs_write((int)a0, (const void *)a1, a2);
    case SYS_OPEN:
        return (u64)vfs_open((const char *)a0, (int)a1);
    case SYS_READ:
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
        return (u64)vfs_list((const char *)a0, (char *)a1, a2);
    default:
        return (u64)-1; /* ENOSYS */
    }
}