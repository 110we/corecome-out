#include "fdt.h"
#include "uart.h"
#include "phys_alloc.h"
#include "mmu.h"
extern char __kernel_start[];
extern char __kernel_end[];

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
    uart_write("\nCOXVBN: BOOT START\n");
    uart_write("COXVBN: UART READY\n");
    uart_write("COXVBN: boot FDT pointer ");
    uart_write_hex((uintptr_t)boot_fdt);
    uart_write("\nCOXVBN: FDT PARSE START\n");

    rc = fdt_parse(boot_fdt, &fdt);
    if (rc != 0) {
        uart_write("COXVBN: FDT PARSE FAILED; memory discovery halted; error ");
        uart_write_hex((uintptr_t)(uint32_t)(-rc));
        uart_write("\n");
        for (;;) __asm__ volatile("wfe");
    }

    uart_write("COXVBN: FDT PARSED\n");
    uart_write("COXVBN: RAM regions ");
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

    uart_write("COXVBN: MEMORY DISCOVERY READY\n");
    rc = phys_allocator_init(&fdt, (uintptr_t)__kernel_start, (uintptr_t)__kernel_end, (uintptr_t)boot_fdt, (uintptr_t)boot_fdt + fdt.total_size);
    if (rc != 0) { uart_write("COXVBN: PHYSICAL ALLOCATOR INIT FAILED\n"); for (;;) __asm__ volatile("wfe"); }
    { struct phys_allocator_stats stats; uintptr_t page; phys_allocator_get_stats(&stats); uart_write("COXVBN: PHYSICAL ALLOCATOR READY; free pages "); uart_write_hex(stats.free_pages); uart_write("\n"); page=phys_alloc_page(); if (!page || phys_free_page(page)!=0) { uart_write("COXVBN: PHYSICAL ALLOCATOR SELFTEST FAILED\n"); for (;;) __asm__ volatile("wfe"); } uart_write("COXVBN: PHYSICAL ALLOCATOR SELFTEST PASS\n"); }

    rc = mmu_enable(&fdt, (uintptr_t)__kernel_start,
                    (uintptr_t)__kernel_end, (uintptr_t)boot_fdt);
    if (rc != 0) {
        uart_write("COXVBN: MMU SETUP FAILED; error ");
        uart_write_hex((uintptr_t)(uint32_t)(-rc));
        uart_write("\n");
        for (;;) __asm__ volatile("wfe");
    }
    uart_write("COXVBN: MMU ENABLED; IDENTITY MAP ACTIVE\n");
    uart_write("COXVBN: BOOT COMPLETE\n");
    for (;;) __asm__ volatile("wfe");
}
