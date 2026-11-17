#include "aarch64_context.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    static uint8_t stack[8192] __attribute__((aligned(16)));
    struct exception_frame *frame = 0;
    uintptr_t base = (uintptr_t)stack;
    uintptr_t top = base + sizeof(stack);

    assert(coxvbn_aarch64_context_build(base, sizeof(stack),
        0x1000U, 0xabcdefU, 0x2000U, &frame) == 0);
    assert(frame != 0);
    assert(((uintptr_t)frame & 0xfU) == 0U);
    assert((uintptr_t)frame >= base);
    assert((uintptr_t)frame + COXVBN_AARCH64_CONTEXT_BYTES <= top);
    assert(frame->x[0] == 0xabcdefU);
    assert(frame->x[30] == 0x2000U);
    assert(frame->elr == 0x1000U);
    assert(frame->spsr == COXVBN_AARCH64_SPSR_EL1H_MASKED);
    assert(frame->esr == 0U && frame->far == 0U && frame->vector == 0U);
    assert(frame->x[1] == 0U && frame->x[29] == 0U);

    frame = (struct exception_frame *)1;
    assert(coxvbn_aarch64_context_build(base, sizeof(stack),
        0x1001U, 0U, 0x2000U, &frame) == -1);
    assert(frame == 0);
    assert(coxvbn_aarch64_context_build(base, 303U,
        0x1000U, 0U, 0x2000U, &frame) == -1);
    assert(coxvbn_aarch64_context_build(base, sizeof(stack),
        0x1000U, 0U, 0U, &frame) == -1);
    assert(coxvbn_aarch64_context_build(base, sizeof(stack),
        0x1000U, 0U, 0x2000U, 0) == -1);
    return 0;
}
