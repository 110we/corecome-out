#include "gicv2.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define GICD_CTLR 0x000U
#define GICD_TYPER 0x004U
#define GICC_CTLR 0x000U
#define GICC_PMR 0x004U
#define GICC_BPR 0x008U

int main(void)
{
    uint32_t distributor[4] = {3U, 31U, 0U, 0U};
    uint32_t cpu_interface[4] = {3U, 0U, 0U, 0U};
    struct gicv2_state state = {0};

    assert(gicv2_init(NULL, (uintptr_t)distributor,
                      (uintptr_t)cpu_interface) == GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, 0U, (uintptr_t)cpu_interface) ==
           GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, (uintptr_t)distributor,
                      (uintptr_t)distributor) == GICV2_ERR_ARGUMENT);

    assert(gicv2_init(&state, (uintptr_t)distributor,
                      (uintptr_t)cpu_interface) == GICV2_OK);
    assert(state.initialized == 1U);
    assert(state.interrupt_lines == 1020U);
    assert(distributor[GICD_CTLR / 4U] == 0U);
    assert(cpu_interface[GICC_CTLR / 4U] == 0U);
    assert(cpu_interface[GICC_PMR / 4U] == 0xffU);
    assert(cpu_interface[GICC_BPR / 4U] == 0U);

    distributor[GICD_TYPER / 4U] = 0U;
    assert(gicv2_init(&state, (uintptr_t)distributor,
                      (uintptr_t)cpu_interface) == GICV2_OK);
    assert(state.interrupt_lines == 32U);
    assert(cpu_interface[GICC_CTLR / 4U] == 0U);

    puts("COXVBN GICv2 quiescent-init tests: PASS");
    return 0;
}
