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
    for (i = 0; i < sizeof(buffer); ++i) uart_putc(buffer[i]);
}

void kernel_main(const void *boot_fdt)
{
    struct fdt_info fdt;
    uint32_t i;
    int rc;
    uart_init();
    uart_write("\nCOXVBN: kernel online\n");
    uart_write("COXVBN: AArch64 early-boot milestone 1\n");
    uart_write("COXVBN: boot FDT pointer ");
    uart_write_hex((uintptr_t)boot_fdt);
    uart_write("\\n");
    rc = fdt_parse(boot_fdt, &fdt);
    if (rc == 0) {
        uart_write("COXVBN: FDT parsed\nCOXVBN: RAM regions ");
        uart_write_hex(fdt.memory_count);
        uart_write("\nCOXVBN: reserved regions ");
        uart_write_hex(fdt.reserved_count);
        uart_write("\n");
        for (i = 0; i < fdt.memory_count; ++i) {
            uart_write("COXVBN: RAM base ");
            uart_write_hex((uintptr_t)fdt.memory[i].base);
            uart_write(" size ");
            uart_write_hex((uintptr_t)fdt.memory[i].size);
            uart_write("\n");
        }
    } else {
        uart_write("COXVBN: FDT parse failed; memory discovery halted; error ");
        uart_write_hex((uintptr_t)(uint32_t)(-rc));
        uart_write("\n");
    }
    for (;;) __asm__ volatile("wfe");
}
