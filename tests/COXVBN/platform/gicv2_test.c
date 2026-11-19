#include "gicv2.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define GICD_CTLR 0x000U
#define GICD_TYPER 0x004U
#define GICD_IGROUPR 0x080U
#define GICD_ISENABLER 0x100U
#define GICD_IPRIORITYR 0x400U
#define GICC_CTLR 0x000U
#define GICC_PMR 0x004U
#define GICC_BPR 0x008U
#define GICC_IAR 0x00cU
#define GICC_EOIR 0x010U

int main(void)
{
    uint32_t distributor[320] = {3U, 31U};
    uint32_t cpu_interface[8] = {3U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    struct gicv2_state state = {0};
    uint32_t token = 0U;

    assert(gicv2_init(NULL, (uintptr_t)distributor, sizeof(distributor),
                      (uintptr_t)cpu_interface, sizeof(cpu_interface)) == GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, 0U, sizeof(distributor),
                      (uintptr_t)cpu_interface, sizeof(cpu_interface)) ==
           GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, (uintptr_t)distributor, sizeof(distributor),
                      (uintptr_t)distributor, sizeof(distributor)) == GICV2_ERR_ARGUMENT);
    assert(gicv2_acknowledge(&state, &token) == GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, (uintptr_t)distributor, 4U,
                      (uintptr_t)cpu_interface, sizeof(cpu_interface)) == GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, (uintptr_t)distributor, sizeof(distributor),
                      (uintptr_t)cpu_interface, 16U) == GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, UINTPTR_MAX - 3U, 16U,
                      (uintptr_t)cpu_interface, sizeof(cpu_interface)) == GICV2_ERR_ARGUMENT);

    assert(gicv2_init(&state, (uintptr_t)distributor, sizeof(distributor),
                      (uintptr_t)cpu_interface, sizeof(cpu_interface)) == GICV2_OK);
    assert(state.initialized == 1U);
    assert(state.interrupt_lines == 1020U);
    assert(distributor[GICD_CTLR / 4U] == 0U);
    assert(cpu_interface[GICC_CTLR / 4U] == 0U);
    assert(cpu_interface[GICC_PMR / 4U] == 0xffU);
    assert(cpu_interface[GICC_BPR / 4U] == 0U);
    assert(gicv2_enable_irq(&state, 3U) == GICV2_ERR_ARGUMENT);
    assert(gicv2_enable_irq(&state, 30U) == GICV2_OK);
    assert((distributor[GICD_IGROUPR / 4U] & (1U << 30)) != 0U);
    assert(distributor[GICD_ISENABLER / 4U] == (1U << 30));
    assert(((uint8_t *)distributor)[GICD_IPRIORITYR + 30U] == 0x80U);
    assert(distributor[GICD_CTLR / 4U] == 2U);
    assert(cpu_interface[GICC_CTLR / 4U] == 2U);

    /* Preserve all IAR bits, including the source CPU ID, for EOI. */
    cpu_interface[GICC_IAR / 4U] = 0x00000a21U;
    assert(gicv2_acknowledge(&state, &token) ==
           GICV2_INTERRUPT_PENDING);
    assert(token == 0x00000a21U);
    assert(gicv2_end_interrupt(&state, token) == GICV2_OK);
    assert(cpu_interface[GICC_EOIR / 4U] == 0x00000a21U);

    cpu_interface[GICC_IAR / 4U] = 1023U;
    assert(gicv2_acknowledge(&state, &token) == GICV2_NO_INTERRUPT);
    assert(token == 1023U);
    assert(gicv2_end_interrupt(&state, token) == GICV2_ERR_ARGUMENT);
    assert(gicv2_end_interrupt(&state, 1020U) == GICV2_ERR_ARGUMENT);

    state.initialized = 0U;
    assert(gicv2_acknowledge(&state, &token) == GICV2_ERR_ARGUMENT);
    assert(gicv2_end_interrupt(&state, 1U) == GICV2_ERR_ARGUMENT);

    state.initialized = 1U;
    distributor[GICD_TYPER / 4U] = 0U;
    assert(gicv2_init(&state, (uintptr_t)distributor, sizeof(distributor),
                      (uintptr_t)cpu_interface, sizeof(cpu_interface)) == GICV2_OK);
    assert(state.interrupt_lines == 32U);
    cpu_interface[GICC_IAR / 4U] = 40U;
    assert(gicv2_acknowledge(&state, &token) == GICV2_NO_INTERRUPT);
    assert(gicv2_end_interrupt(&state, 40U) == GICV2_ERR_ARGUMENT);
    assert(cpu_interface[GICC_CTLR / 4U] == 0U);

    puts("COXVBN GICv2 quiescent-init and acknowledge/EOI tests: PASS");
    return 0;
}
