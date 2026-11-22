#include "gicv2.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define GICD_CTLR 0x000U
#define GICD_TYPER 0x004U
#define GICC_CTLR 0x000U
#define GICC_PMR 0x004U
#define GICC_BPR 0x008U
#define GICC_IAR 0x00cU
#define GICC_EOIR 0x010U
#define GICC_AIAR 0x020U
#define GICC_AEOIR 0x024U

int main(void)
{
    uint32_t distributor[0x200] = {3U, 31U | (1U << 10)};
    uint32_t cpu_interface[16] = {3U};
    uint8_t *distributor_bytes = (uint8_t *)distributor;
    struct gicv2_state state = {0};
    uint32_t token = 0U;

    assert(gicv2_init(NULL, (uintptr_t)distributor,
                      (uintptr_t)cpu_interface) == GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, 0U, (uintptr_t)cpu_interface) ==
           GICV2_ERR_ARGUMENT);
    assert(gicv2_init(&state, (uintptr_t)distributor,
                      (uintptr_t)distributor) == GICV2_ERR_ARGUMENT);
    assert(gicv2_acknowledge(&state, &token) == GICV2_ERR_ARGUMENT);

    assert(gicv2_init(&state, (uintptr_t)distributor,
                      (uintptr_t)cpu_interface) == GICV2_OK);
    assert(state.initialized == 1U);
    assert(state.interrupt_lines == 1020U);
    assert(distributor[GICD_CTLR / 4U] == 0U);
    assert(cpu_interface[GICC_CTLR / 4U] == 0U);
    assert(cpu_interface[GICC_PMR / 4U] == 0xffU);
    assert(cpu_interface[GICC_BPR / 4U] == 0U);

    /* Configure the QEMU ARMv8 non-secure physical timer PPI (INTID 30). */
    assert(gicv2_enable_ppi(&state, 15U, 0x80U) == GICV2_ERR_ARGUMENT);
    assert(gicv2_enable_ppi(&state, 32U, 0x80U) == GICV2_ERR_ARGUMENT);
    assert(gicv2_enable_ppi(&state, 30U, 0x80U) == GICV2_OK);
    assert(distributor[0x180U / 4U] == (1U << 30));
    assert((distributor[0x080U / 4U] & (1U << 30)) != 0U);
    assert(distributor_bytes[0x400U + 30U] == 0x80U);
    assert(distributor[0x280U / 4U] == (1U << 30));
    assert(distributor[0x100U / 4U] == (1U << 30));
    assert(gicv2_enable_group1(&state) == GICV2_OK);
    assert(distributor[GICD_CTLR / 4U] == 2U);
    assert(cpu_interface[GICC_CTLR / 4U] == 2U);

    /* Preserve all IAR bits, including the source CPU ID, for EOI. */
    assert(state.security_extensions == 1U);
    assert(state.security_disabled == 0U);
    cpu_interface[GICC_AIAR / 4U] = 0x00000a21U;
    assert(gicv2_acknowledge(&state, &token) ==
           GICV2_INTERRUPT_PENDING);
    assert(token == 0x00000a21U);
    assert(gicv2_end_interrupt(&state, token) == GICV2_OK);
    assert(cpu_interface[GICC_AEOIR / 4U] == 0x00000a21U);

    cpu_interface[GICC_AIAR / 4U] = 1023U;
    assert(gicv2_acknowledge(&state, &token) == GICV2_NO_INTERRUPT);
    assert(token == 1023U);
    assert(gicv2_end_interrupt(&state, token) == GICV2_ERR_ARGUMENT);
    assert(gicv2_end_interrupt(&state, 1020U) == GICV2_ERR_ARGUMENT);

    state.initialized = 0U;
    assert(gicv2_acknowledge(&state, &token) == GICV2_ERR_ARGUMENT);
    assert(gicv2_end_interrupt(&state, 1U) == GICV2_ERR_ARGUMENT);
    assert(gicv2_enable_group1(&state) == GICV2_ERR_ARGUMENT);

    state.initialized = 1U;
    distributor[GICD_TYPER / 4U] = 0U;
    distributor[0x080U / 4U] = 0U;
    assert(gicv2_init(&state, (uintptr_t)distributor,
                      (uintptr_t)cpu_interface) == GICV2_OK);
    assert(state.interrupt_lines == 32U);
    assert(state.security_extensions == 0U);
    assert(gicv2_enable_ppi(&state, 30U, 0x80U) == GICV2_OK);
    assert(distributor[0x080U / 4U] == 0U);
    assert(gicv2_enable_group1(&state) == GICV2_OK);
    assert(distributor[GICD_CTLR / 4U] == 1U);
    assert(cpu_interface[GICC_CTLR / 4U] == 1U);
    cpu_interface[GICC_IAR / 4U] = 0x0000001fU;
    assert(gicv2_acknowledge(&state, &token) ==
           GICV2_INTERRUPT_PENDING);
    assert(token == 0x0000001fU);
    assert(gicv2_end_interrupt(&state, token) == GICV2_OK);
    assert(cpu_interface[GICC_EOIR / 4U] == 0x0000001fU);
    cpu_interface[GICC_IAR / 4U] = 40U;
    assert(gicv2_acknowledge(&state, &token) == GICV2_NO_INTERRUPT);
    assert(gicv2_end_interrupt(&state, 40U) == GICV2_ERR_ARGUMENT);
    assert(cpu_interface[GICC_CTLR / 4U] == 1U);

    puts("COXVBN GICv2 quiescent-init and acknowledge/EOI tests: PASS");
    return 0;
}
