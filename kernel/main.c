#include "uart.h"

void kernel_main(void)
{
    uart_init();
    uart_write("\nCORECOME: kernel online\n");
    uart_write("CORECOME: AArch64 early-boot milestone 1\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
