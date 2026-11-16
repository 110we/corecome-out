#include "exception.h"
#include "gicv2.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t distributor_regs[16];
static uint32_t cpu_regs[16];
static uint32_t handled_id;
static unsigned int handled_count;

void uart_write(const char *text) { (void)text; }
void uart_putc(char c) { (void)c; }

static void test_handler(uint32_t id, void *context)
{
    uint32_t *value = (uint32_t *)context;
    handled_id = id;
    *value += 1U;
    handled_count++;
}

int main(void)
{
    struct gicv2_state gic = {
        .distributor_base = (uintptr_t)distributor_regs,
        .cpu_interface_base = (uintptr_t)cpu_regs,
        .distributor_size = sizeof(distributor_regs),
        .cpu_interface_size = sizeof(cpu_regs),
        .interrupt_lines = 64U,
        .initialized = 1U
    };
    struct exception_frame frame = { {0}, 0x1000U, 0U, 0U, 0U, 5U };
    uint32_t context_count = 0U;
    uint32_t token = (2U << 10) | 5U;

    cpu_regs[3] = token; /* GICC_IAR */
    exception_set_gicv2(&gic);
    assert(exception_register_irq_handler(5U, test_handler,
                                           &context_count) == 0);
    assert(exception_register_irq_handler(5U, test_handler,
                                           &context_count) != 0);

    exception_dispatch(&frame);
    assert(handled_count == 1U);
    assert(handled_id == 5U);
    assert(context_count == 1U);
    assert(cpu_regs[4] == token); /* GICC_EOIR receives the complete token. */

    /* Current-EL SPx IRQ uses vector slot 13, not lower-AArch32 slot 9. */
    frame.vector = 13U;
    cpu_regs[3] = token;
    exception_dispatch(&frame);
    assert(handled_count == 2U);
    assert(context_count == 2U);
    assert(cpu_regs[4] == token);

    assert(exception_unregister_irq_handler(5U) == 0);
    assert(exception_unregister_irq_handler(5U) != 0);

    /* A spurious interrupt returns without dispatching or writing EOI. */
    frame.vector = 5U;
    cpu_regs[3] = 1023U;
    cpu_regs[4] = 0U;
    exception_dispatch(&frame);
    assert(handled_count == 2U);
    assert(cpu_regs[4] == 0U);

    puts("exception dispatch tests: PASS");
    return 0;
}
