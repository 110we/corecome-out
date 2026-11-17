#include "exception.h"
#include "gicv2.h"
#include "scheduler.h"

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
        (uintptr_t)distributor_regs,
        (uintptr_t)cpu_regs,
        64U,
        1U
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

    assert(exception_dispatch(&frame) == &frame);
    assert(handled_count == 1U);
    assert(handled_id == 5U);
    assert(context_count == 1U);
    assert(cpu_regs[4] == token); /* GICC_EOIR receives the complete token. */

    assert(exception_unregister_irq_handler(5U) == 0);
    assert(exception_unregister_irq_handler(5U) != 0);

    /* A spurious interrupt returns without dispatching or writing EOI. */
    cpu_regs[3] = 1023U;
    cpu_regs[4] = 0U;
    assert(exception_dispatch(&frame) == &frame);
    assert(handled_count == 1U);
    assert(cpu_regs[4] == 0U);

    assert(exception_register_irq_handler(5U, test_handler,
                                           &context_count) == 0);
    {
        static struct coxvbn_scheduler scheduler;
        static uint8_t stacks[2][COXVBN_TASK_MIN_STACK_SIZE]
            __attribute__((aligned(16)));
        struct coxvbn_task_ref first, second;
        struct exception_frame *frames[2];
        struct exception_frame *selected;

        frames[0] = (struct exception_frame *)(void *)
            (stacks[0] + sizeof(stacks[0]) - 304U);
        frames[1] = (struct exception_frame *)(void *)
            (stacks[1] + sizeof(stacks[1]) - 304U);
        frames[0]->vector = 5U;
        frames[1]->vector = 5U;
        coxvbn_scheduler_init(&scheduler);
        assert(coxvbn_task_create(&scheduler, 0x1000U, 0U,
            (uintptr_t)stacks[0], sizeof(stacks[0]), 0, &first) == 0);
        assert(coxvbn_task_create(&scheduler, 0x2000U, 0U,
            (uintptr_t)stacks[1], sizeof(stacks[1]), 0, &second) == 0);
        assert(coxvbn_task_set_initial_frame(&scheduler, &first, frames[0]) == 0);
        assert(coxvbn_task_set_initial_frame(&scheduler, &second, frames[1]) == 0);
        selected = coxvbn_scheduler_schedule_frame(&scheduler, 0);
        assert(selected == frames[0]);
        exception_set_scheduler(&scheduler);

        cpu_regs[3] = token;
        assert(exception_dispatch(frames[0]) == frames[1]);
        cpu_regs[3] = token;
        assert(exception_dispatch(frames[1]) == frames[0]);
        exception_set_scheduler(0);
    }
    assert(exception_unregister_irq_handler(5U) == 0);

    puts("exception dispatch tests: PASS");
    return 0;
}
