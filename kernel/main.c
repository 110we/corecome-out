#include "fdt.h"
#include "uart.h"
#include "phys_alloc.h"
#include "mmu.h"
#include "gicv2.h"
#include "exception.h"
#include "arm_timer.h"
#include "vfs.h"
#include "scheduler.h"
#include "aarch64_context.h"
static struct coxvbn_vfs boot_vfs;
static struct coxvbn_scheduler boot_scheduler;
static struct gicv2_state boot_gic;
static uint8_t boot_task_stacks[2][COXVBN_TASK_MIN_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t boot_task_a_runs;
static volatile uint32_t boot_task_b_runs;
static volatile uint32_t boot_task_a_exited;
static volatile uint32_t boot_task_b_after_exit_reported;
static volatile uint32_t boot_task_b_timer_preempt_reported;
static volatile uint32_t boot_timer_ticks;

extern void coxvbn_aarch64_enter_frame(struct exception_frame *frame)
    __attribute__((noreturn));

static void coxvbn_task_yield_cpu(void)
{
    __asm__ volatile("svc #0" ::: "memory");
}

static void coxvbn_timer_irq(uint32_t interrupt_id, void *context)
{
    uint32_t programmed_ticks;
    (void)interrupt_id;
    (void)context;

    if (arm_timer_start_oneshot_ns(10000000ULL, &programmed_ticks) !=
        ARM_TIMER_OK) {
        (void)arm_timer_disable();
        uart_write("COXVBN: TIMER IRQ REARM FAILED\n");
        return;
    }

    ++boot_timer_ticks;
    if (boot_timer_ticks == 1U)
        uart_write("COXVBN: TIMER IRQ FIRST TICK\n");
    if (boot_timer_ticks == 3U)
        uart_write("COXVBN: TIMER IRQ PASS\n");
}

static int coxvbn_task_a(void *argument)
{
    uint32_t i;
    uint64_t start;
    uint64_t wait_ticks;
    (void)argument;

    /*
     * Deliberately do not yield here: the timer PPI must preempt this task
     * and dispatch task B on its separate stack.
     */
    start = arm_timer_now();
    wait_ticks = arm_timer_frequency() / 20U;
    if (wait_ticks == 0U) wait_ticks = 1U;
    while ((arm_timer_now() - start) < wait_ticks)
        __asm__ volatile("nop");

    for (i = 0U; i < 3U; ++i) {
        ++boot_task_a_runs;
        coxvbn_task_yield_cpu();
    }
    uart_write("COXVBN: TASK A COOPERATIVE SWITCH PASS\n");
    return 42;
}

static void coxvbn_task_b(void *argument)
{
    (void)argument;
    for (;;) {
        if (boot_task_b_runs < 3U) {
            ++boot_task_b_runs;
            if (boot_task_b_runs == 3U)
                uart_write("COXVBN: TASK B COOPERATIVE SWITCH PASS\n");
        }
        if (boot_timer_ticks != 0U &&
            boot_task_b_timer_preempt_reported == 0U) {
            boot_task_b_timer_preempt_reported = 1U;
            uart_write("COXVBN: TASK B TIMER PREEMPTION PASS\n");
        }
        if (boot_task_a_exited != 0U &&
            boot_task_b_after_exit_reported == 0U) {
            boot_task_b_after_exit_reported = 1U;
            uart_write("COXVBN: TASK B AFTER TASK A EXIT PASS\n");
        }
        coxvbn_task_yield_cpu();
    }
}

static void __attribute__((noreturn))
coxvbn_task_return_trampoline(int exit_code)
{
    register uintptr_t result __asm__("x0") = (uintptr_t)(intptr_t)exit_code;
    if (exit_code == 42)
        boot_task_a_exited = 1U;
    __asm__ volatile("svc #1" : "+r"(result) :: "memory");
    for (;;) __asm__ volatile("wfe");
}

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
    struct coxvbn_task_ref task_a, task_b;
    struct exception_frame *frame_a = 0, *frame_b = 0;
    struct exception_frame *dispatch_frame;
    uint32_t initial_timer_ticks;

    uart_init();
    uart_write("\nCOXVBN: BOOT START\n");
    coxvbn_vfs_init(&boot_vfs);
    if (coxvbn_vfs_mkdir(&boot_vfs, "/etc") != 0 ||
        coxvbn_vfs_create(&boot_vfs, "/etc/kernel.conf") != 0) {
        uart_write("COXVBN: VFS INITIALIZATION FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }
    {
        static const char config[] = "name=COXVBN";
        char verify[sizeof(config)];
        size_t written = 0U, read_count = 0U;
        if (coxvbn_vfs_write(&boot_vfs, "/etc/kernel.conf", 0U,
                             config, sizeof(config) - 1U, &written) != 0 ||
            written != sizeof(config) - 1U ||
            coxvbn_vfs_read(&boot_vfs, "/etc/kernel.conf", 0U,
                            verify, sizeof(verify) - 1U, &read_count) != 0 ||
            read_count != written) {
            uart_write("COXVBN: VFS SELFTEST FAILED\n");
            for (;;) __asm__ volatile("wfe");
        }
        {
            size_t verify_index;
            for (verify_index = 0U; verify_index < written; ++verify_index) {
                if (verify[verify_index] != config[verify_index]) {
                    uart_write("COXVBN: VFS SELFTEST DATA MISMATCH\n");
                    for (;;) __asm__ volatile("wfe");
                }
            }
        }
        uart_write("COXVBN: VFS IN-MEMORY READY; SELFTEST PASS\n");
    }
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
    if (fdt.armv8_timer_found != 0U) {
        uart_write("COXVBN: ARMV8 TIMER NONSECURE PHYSICAL PPI ");
        uart_write_hex((uintptr_t)fdt.armv8_timer_ppi);
        uart_write("\n");
    } else {
        uart_write("COXVBN: ARMV8 TIMER PPI NOT FOUND\n");
    }
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
        const uint64_t aperture_start = 0x08000000ULL;
        const uint64_t aperture_end = 0x08200000ULL;
        if (fdt.gicv2_distributor.size < 0x1000U ||
            fdt.gicv2_cpu_interface.size < 0x1000U ||
            fdt.gicv2_distributor.base < aperture_start ||
            fdt.gicv2_cpu_interface.base < aperture_start ||
            fdt.gicv2_distributor.base > aperture_end - 0x1000U ||
            fdt.gicv2_cpu_interface.base > aperture_end - 0x1000U) {
            uart_write("COXVBN: GICV2 FDT RANGES OUTSIDE BOOT MMIO APERTURE\n");
            for (;;) __asm__ volatile("wfe");
        }
        rc = gicv2_init(&boot_gic, (uintptr_t)fdt.gicv2_distributor.base,
                        (uintptr_t)fdt.gicv2_cpu_interface.base);
        if (rc != GICV2_OK) {
            uart_write("COXVBN: GICV2 QUIESCENT INIT FAILED\n");
            for (;;) __asm__ volatile("wfe");
        }
        exception_set_gicv2(&boot_gic);
        uart_write("COXVBN: GICV2 DISCOVERED AND QUIESCED; PPI SETUP PENDING\n");
    } else {
        uart_write("COXVBN: NO SUPPORTED GICV2 NODE IN FDT\n");
    }
    /* Enter two EL1 tasks on their own kernel stacks. */
    coxvbn_scheduler_init(&boot_scheduler);
    if (coxvbn_task_create(&boot_scheduler,
            (uintptr_t)coxvbn_task_a, 0U,
            (uintptr_t)boot_task_stacks[0], sizeof(boot_task_stacks[0]),
            0, &task_a) != 0 ||
        coxvbn_aarch64_context_build((uintptr_t)boot_task_stacks[0],
            sizeof(boot_task_stacks[0]), (uintptr_t)coxvbn_task_a, 0U,
            (uintptr_t)coxvbn_task_return_trampoline, &frame_a) != 0 ||
        coxvbn_task_set_initial_frame(&boot_scheduler, &task_a, frame_a) != 0 ||
        coxvbn_task_create(&boot_scheduler,
            (uintptr_t)coxvbn_task_b, 0U,
            (uintptr_t)boot_task_stacks[1], sizeof(boot_task_stacks[1]),
            0, &task_b) != 0 ||
        coxvbn_aarch64_context_build((uintptr_t)boot_task_stacks[1],
            sizeof(boot_task_stacks[1]), (uintptr_t)coxvbn_task_b, 0U,
            (uintptr_t)coxvbn_task_return_trampoline, &frame_b) != 0 ||
        coxvbn_task_set_initial_frame(&boot_scheduler, &task_b, frame_b) != 0) {
        uart_write("COXVBN: TASK CONTEXT SETUP FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }
    if (boot_gic.initialized != 1U ||
        fdt.armv8_timer_found == 0U || fdt.armv8_timer_ppi != 30U) {
        uart_write("COXVBN: TIMER/GIC PLATFORM BINDING FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }

    /*
     * The first-run frames are built masked by default. Only unmask IRQ in
     * task context after the handler, PPI, and both GIC interfaces are ready.
     */
    frame_a->spsr = COXVBN_AARCH64_SPSR_EL1H_IRQ_ENABLED;
    frame_b->spsr = COXVBN_AARCH64_SPSR_EL1H_IRQ_ENABLED;
    exception_set_scheduler(&boot_scheduler);
    if (exception_register_irq_handler(fdt.armv8_timer_ppi,
                                       coxvbn_timer_irq, 0) != 0) {
        uart_write("COXVBN: TIMER IRQ HANDLER REGISTRATION FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }
    if (gicv2_enable_ppi(&boot_gic, fdt.armv8_timer_ppi, 0x80U) != GICV2_OK ||
        gicv2_enable_group1(&boot_gic) != GICV2_OK) {
        uart_write("COXVBN: GICV2 TIMER PPI ENABLE FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }
    if (arm_timer_start_oneshot_ns(10000000ULL,
                                   &initial_timer_ticks) != ARM_TIMER_OK) {
        uart_write("COXVBN: PHYSICAL TIMER START FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }
    uart_write("COXVBN: GICV2 GROUP1 ENABLED; TIMER PPI 30 ARMED\n");
    uart_write("COXVBN: BOOT COMPLETE\n");

    dispatch_frame = coxvbn_scheduler_schedule_frame(&boot_scheduler, 0);
    if (dispatch_frame == 0) {
        uart_write("COXVBN: FIRST TASK DISPATCH FAILED\n");
        for (;;) __asm__ volatile("wfe");
    }
    uart_write("COXVBN: FIRST TASK DISPATCH\n");
    coxvbn_aarch64_enter_frame(dispatch_frame);
}
