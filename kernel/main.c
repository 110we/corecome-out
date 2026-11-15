#include "fdt.h"
#include "uart.h"

static void uart_write_hex(uintptr_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[2U * sizeof(uintptr_t)];
    unsigned int i;

    for (i = 0; i < sizeof(buffer); ++i) {
        buffer[sizeof(buffer) - 1U - i] = digits[value & 0xfU];
        value >>= 4;
    }

    uart_write("0x");
    for (i = 0; i < sizeof(buffer); ++i) {
        uart_putc(buffer[i]);
    }
}

void kernel_main(const void *boot_fdt)
{
    struct fdt_info fdt;

    uart_init();
    uart_write("\nCOXVBN: kernel online\n");
    uart_write("COXVBN: AArch64 early-boot milestone 1\n");

    if (fdt_inspect(boot_fdt, &fdt) == 0) {
        uart_write("COXVBN: FDT validated at ");
        uart_write_hex(fdt.address);
        uart_write("\nCOXVBN: FDT bytes ");
        uart_write_hex(fdt.total_size);
        uart_write("\n");
    } else {
        uart_write("COXVBN: no valid FDT supplied; platform discovery halted\n");
    }

    for (;;) {
        __asm__ volatile("wfe");
    }
}
