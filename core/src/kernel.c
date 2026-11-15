#include "corecome/uart.h"

void kernel_main(void)
{
    uart_init();
    uart_puts("\nCORECOME standalone security kernel\n");
    uart_puts("arch: AArch64 | privilege: EL1 | platform: QEMU virt\n");
    uart_puts("boot: PASS | next: exceptions, MMU, allocator, scheduler\n");
    for (;;) {
        __asm__ volatile("wfe");
    }
}
