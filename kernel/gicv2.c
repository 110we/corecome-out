#include "gicv2.h"

#include <stddef.h>

#define GICD_CTLR 0x000U
#define GICD_TYPER 0x004U
#define GICC_CTLR 0x000U
#define GICC_PMR 0x004U
#define GICC_BPR 0x008U
#define GICC_IAR 0x00cU
#define GICC_EOIR 0x010U
#define GICD_CTLR_ENABLE_MASK 0x3U
#define GICC_CTLR_ENABLE_MASK 0x3U
#define GICD_TYPER_ITLINES_MASK 0x1fU
#define GICV2_MAX_LINES 1020U
#define GICV2_INTERRUPT_ID_MASK 0x3ffU

static volatile uint32_t *reg32(uintptr_t base, uintptr_t offset)
{
    return (volatile uint32_t *)(base + offset);
}

static void mmio_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy\n\tisb" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

int gicv2_init(struct gicv2_state *state,
               uintptr_t distributor_base,
               uintptr_t cpu_interface_base)
{
    uint32_t typer;
    uint32_t lines;

    if (state == NULL || distributor_base == 0U ||
        cpu_interface_base == 0U ||
        (distributor_base & 3U) != 0U ||
        (cpu_interface_base & 3U) != 0U ||
        distributor_base == cpu_interface_base)
        return GICV2_ERR_ARGUMENT;

    state->distributor_base = distributor_base;
    state->cpu_interface_base = cpu_interface_base;
    state->interrupt_lines = 0U;
    state->initialized = 0U;

    /* Disable delivery before inspecting controller capabilities. */
    *reg32(cpu_interface_base, GICC_CTLR) = 0U;
    *reg32(distributor_base, GICD_CTLR) = 0U;
    mmio_barrier();

    if ((*reg32(cpu_interface_base, GICC_CTLR) &
         GICC_CTLR_ENABLE_MASK) != 0U ||
        (*reg32(distributor_base, GICD_CTLR) &
         GICD_CTLR_ENABLE_MASK) != 0U)
        return GICV2_ERR_MMIO;

    *reg32(cpu_interface_base, GICC_PMR) = 0xffU;
    *reg32(cpu_interface_base, GICC_BPR) = 0U;
    mmio_barrier();

    typer = *reg32(distributor_base, GICD_TYPER);
    lines = ((typer & GICD_TYPER_ITLINES_MASK) + 1U) * 32U;
    if (lines > GICV2_MAX_LINES) lines = GICV2_MAX_LINES;

    state->interrupt_lines = lines;
    state->initialized = 1U;
    return GICV2_OK;
}

int gicv2_acknowledge(struct gicv2_state *state, uint32_t *acknowledge)
{
    uint32_t token;
    uint32_t interrupt_id;

    if (state == NULL || acknowledge == NULL ||
        state->initialized != 1U || state->cpu_interface_base == 0U)
        return GICV2_ERR_ARGUMENT;

    token = *reg32(state->cpu_interface_base, GICC_IAR);
    interrupt_id = token & GICV2_INTERRUPT_ID_MASK;
    if (interrupt_id >= GICV2_MAX_LINES ||
        interrupt_id >= state->interrupt_lines) {
        *acknowledge = token;
        return GICV2_NO_INTERRUPT;
    }

    *acknowledge = token;
    mmio_barrier();
    return GICV2_INTERRUPT_PENDING;
}

int gicv2_end_interrupt(struct gicv2_state *state, uint32_t acknowledge)
{
    uint32_t interrupt_id = acknowledge & GICV2_INTERRUPT_ID_MASK;

    if (state == NULL || state->initialized != 1U ||
        state->cpu_interface_base == 0U ||
        (interrupt_id >= GICV2_MAX_LINES ||
         interrupt_id >= state->interrupt_lines))
        return GICV2_ERR_ARGUMENT;

    /* GICv2 requires the original IAR token, including source CPU ID bits. */
    mmio_barrier();
    *reg32(state->cpu_interface_base, GICC_EOIR) = acknowledge;
    mmio_barrier();
    return GICV2_OK;
}
