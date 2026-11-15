#ifndef COXVBN_GICV2_H
#define COXVBN_GICV2_H

#include <stdint.h>

#define GICV2_OK 0
#define GICV2_ERR_ARGUMENT -1
#define GICV2_ERR_MMIO -2

struct gicv2_state {
    uintptr_t distributor_base;
    uintptr_t cpu_interface_base;
    uint32_t interrupt_lines;
    uint32_t initialized;
};

/*
 * Quiesce and identify a GICv2 instance. This deliberately leaves both
 * interfaces disabled; IRQ delivery is not enabled until exception return,
 * dispatch, and EOI handling are implemented.
 */
int gicv2_init(struct gicv2_state *state,
               uintptr_t distributor_base,
               uintptr_t cpu_interface_base);

#endif
