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
    uint32_t security_extensions;
    uint32_t security_disabled;
    /* Single-core IRQ entry masks nesting, so one outstanding token is enough. */
    uint32_t last_ack_alias;
};

/*
 * Quiesce and identify a GICv2 instance. Both interfaces remain disabled;
 * IRQ delivery is not enabled until exception return and dispatch are ready.
 */
int gicv2_init(struct gicv2_state *state,
               uintptr_t distributor_base,
               uintptr_t cpu_interface_base);

/*
 * Read the Group 1 acknowledge token from GICC_IAR or the secure GICC_AIAR
 * alias. Preserve it unchanged for gicv2_end_interrupt() including source CPU ID.
 * Returns PENDING, NO_INTERRUPT for architecturally spurious IDs, or error.
 */
int gicv2_acknowledge(struct gicv2_state *state, uint32_t *acknowledge);

/* Write the original token to GICC_EOIR or GICC_AEOIR, matching the read alias. */
int gicv2_end_interrupt(struct gicv2_state *state, uint32_t acknowledge);

/* Configure and enable a banked PPI as Group 1 while the CPU interface is off. */
int gicv2_enable_ppi(struct gicv2_state *state, uint32_t interrupt_id,
                     uint8_t priority);

/* Enable interrupt forwarding for the controller's detected group model. */
int gicv2_enable_interrupts(struct gicv2_state *state);

#endif
