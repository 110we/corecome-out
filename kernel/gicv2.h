#ifndef COXVBN_GICV2_H
#define COXVBN_GICV2_H

#include <stdint.h>

#define GICV2_OK 0
#define GICV2_ERR_ARGUMENT -1
#define GICV2_ERR_MMIO -2
#define GICV2_NO_INTERRUPT 0
#define GICV2_INTERRUPT_PENDING 1

struct gicv2_state {
    uintptr_t distributor_base;
    uintptr_t cpu_interface_base;
    uint32_t interrupt_lines;
    uint32_t initialized;
};

/*
 * Quiesce and identify a GICv2 instance. Both interfaces remain disabled;
 * IRQ delivery is not enabled until exception return and dispatch are ready.
 */
int gicv2_init(struct gicv2_state *state,
               uintptr_t distributor_base,
               uintptr_t cpu_interface_base);

/*
 * Read the GICC_IAR acknowledge token. The complete token must be preserved
 * and passed unchanged to gicv2_end_interrupt() so the source CPU ID is kept.
 * Returns PENDING, NO_INTERRUPT for architecturally spurious IDs, or error.
 */
int gicv2_acknowledge(struct gicv2_state *state, uint32_t *acknowledge);

/* Write the original acknowledge token to GICC_EOIR. */
int gicv2_end_interrupt(struct gicv2_state *state, uint32_t acknowledge);

/* Configure and enable a banked PPI as Group 1 while the CPU interface is off. */
int gicv2_enable_ppi(struct gicv2_state *state, uint32_t interrupt_id,
                     uint8_t priority);

/* Enable Group 1 at the distributor and current CPU interface. */
int gicv2_enable_group1(struct gicv2_state *state);

#endif
