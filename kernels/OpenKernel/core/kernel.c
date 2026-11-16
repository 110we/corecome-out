#include "../platform/uart.h"

void openkernel_main(unsigned long boot_info)
{
    (void)boot_info;
    openkernel_uart_init();
    openkernel_uart_write("\nOpenKernel: independent AArch64 kernel entry reached.\n");
    openkernel_uart_write("OpenKernel: scheduler, MMU, drivers and user ABI are not implemented yet.\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
