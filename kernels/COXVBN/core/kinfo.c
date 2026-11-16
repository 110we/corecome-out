/* kinfo.c —— 把内核自己长什么样打印出来。
 *
 * 不做“硬编码的架构图”，全部取自运行时：链接器给的符号、堆/页池的计数、
 * 线程表、当前 aspace 的 L3 表、系统调用表。所以这份输出能当断言用——
 * 数字不对就是真的不对。
 */
#include "kern.h"

extern u8 __bss_start[];
extern u8 __bss_end[];
extern u8 __stack_bottom[];
extern u8 __stack_top[];
extern void _start(void);

void kernel_structure(void)
{
    u64 sctlr;
    u64 ttbr;

    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));

    printk("==== kernel structure ====\n");
    printk("[image] %x..%x .text/.rodata/.data\n", (u32)(u64)&_start,
           (u32)(u64)__bss_start);
    printk("[image] %x..%x .bss (%u bytes)\n", (u32)(u64)__bss_start,
           (u32)(u64)__bss_end, (u32)(u64)(__bss_end - __bss_start));
    printk("[stack] %x..%x (%uK)\n", (u32)(u64)__stack_bottom,
           (u32)(u64)__stack_top,
           (u32)((u64)(__stack_top - __stack_bottom) / 1024));
    printk("[mem] heap %u/%u B, pages free %u/%u\n", (u32)heap_used(),
           (u32)heap_size(), (u32)mm_pages_free(), (u32)mm_pages_total());
    printk("[cpu] sctlr=%x ttbr0=%x (4K granule, MMU on)\n", (u32)sctlr,
           (u32)ttbr);

    sched_dump();
    pg_dump(sched_get_l1());
    syscall_dump();

    printk("[perm] L1 页表(U/AP/UXN) | L2 EL0 故障杀线程 | L3 u_ok/u_str\n");
    printk("==== end ====\n");
}