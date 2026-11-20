#include "fdt.h"
#include "uart.h"
#include "phys_alloc.h"
#include "mmu.h"
#include "gicv2.h"
#include "exception.h"
#include "arm_timer.h"
extern char __kernel_start[];
extern char __kernel_end[];

static int mmio_region_in_aperture(uint64_t base, uint64_t size,
                                   uint64_t aperture_start,
                                   uint64_t aperture_end,
                                   uint64_t minimum_size)
{
    return base >= aperture_start && base <= aperture_end &&
           size >= minimum_size && size <= aperture_end - base;
}

static volatile uint32_t timer_irq_count;

static void timer_irq_handler(uint32_t interrupt_id, void *context)
{
    (void)interrupt_id;
    (void)context;
    (void)arm_timer_disable();
    ++timer_irq_count;
    uart_write("COXVBN: TIMER IRQ HANDLED\n");
}

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
    rc = arm_timer_init();
    if (rc != ARM_TIMER_OK) {
        uart_write("COXVBN: ARM GENERIC TIMER INIT FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }
    uart_write("COXVBN: ARM GENERIC TIMER READY; CNTFRQ ");
    uart_write_hex((uintptr_t)arm_timer_frequency());
    uart_write(" CNT ");
    uart_write_hex((uintptr_t)arm_timer_now());
    uart_write("; PHYSICAL TIMER IRQ MASKED\n");
    if (fdt.gicv2_found != 0U) {
        struct gicv2_state gic;
        const uint64_t aperture_start = 0x08000000ULL;
        const uint64_t aperture_end = 0x08200000ULL;
        if (!mmio_region_in_aperture(
                fdt.gicv2_distributor.base,
                fdt.gicv2_distributor.size,
                aperture_start, aperture_end, 0x1000U) ||
            !mmio_region_in_aperture(
                fdt.gicv2_cpu_interface.base,
                fdt.gicv2_cpu_interface.size,
                aperture_start, aperture_end, 0x1000U)) {
            uart_write("COXVBN: GICV2 FDT RANGES OUTSIDE BOOT MMIO APERTURE\n");
            for (;;) __asm__ volatile("wfe");
        }
        rc = gicv2_init(&gic,
                        (uintptr_t)fdt.gicv2_distributor.base,
                        (uintptr_t)fdt.gicv2_distributor.size,
                        (uintptr_t)fdt.gicv2_cpu_interface.base,
                        (uintptr_t)fdt.gicv2_cpu_interface.size);
        if (rc != GICV2_OK) {
            uart_write("COXVBN: GICV2 QUIESCENT INIT FAILED\n");
            for (;;) __asm__ volatile("wfe");
        }
        exception_set_gicv2(&gic);
        if (exception_register_irq_handler(30U, timer_irq_handler, 0) != 0 ||
            gicv2_enable_irq(&gic, 30U) != GICV2_OK) {
            uart_write("COXVBN: TIMER PPI REGISTRATION FAILED\n");
            for (;;) __asm__ volatile("wfe");
        }
        {
            uint32_t programmed_ticks;
            uint64_t start_count, deadline_ticks;
            if (arm_timer_set_oneshot_ns(10000000ULL,
                                         &programmed_ticks) != ARM_TIMER_OK) {
                uart_write("COXVBN: TIMER ONESHOT PROGRAM FAILED\n");
                for (;;) __asm__ volatile("wfe");
            }
            uart_write("COXVBN: GICV2 GROUP0 ENABLED; TIMER PPI 30 ARMED\n");
            start_count = arm_timer_now();
            deadline_ticks = arm_timer_frequency() * 2ULL;
            __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
            while (timer_irq_count == 0U &&
                   arm_timer_now() - start_count < deadline_ticks)
                __asm__ volatile("wfe");
            __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
            if (timer_irq_count == 0U) {
                uart_write("COXVBN: TIMER IRQ SELFTEST FAILED\n");
                for (;;) __asm__ volatile("wfe");
            }
            uart_write("COXVBN: TIMER IRQ SELFTEST PASS\n");
        }
    } else {
        uart_write("COXVBN: NO SUPPORTED GICV2 NODE IN FDT\n");
    }
    uart_write("COXVBN: BOOT COMPLETE\n");
    for (;;) __asm__ volatile("wfe");
}
