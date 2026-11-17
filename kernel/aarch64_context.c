#include "aarch64_context.h"

#include <limits.h>

int coxvbn_aarch64_context_build(uintptr_t stack_base, size_t stack_size,
                                 uintptr_t entry, uintptr_t argument,
                                 uintptr_t return_trampoline,
                                 struct exception_frame **frame_out)
{
    uintptr_t stack_top;
    uintptr_t frame_address;
    struct exception_frame *frame;
    uint64_t *storage;
    size_t i;

    if (frame_out == 0)
        return -1;
    *frame_out = 0;
    if (stack_base == 0U || entry == 0U || return_trampoline == 0U ||
        (stack_base & 0xfU) != 0U || (entry & 0x3U) != 0U ||
        (return_trampoline & 0x3U) != 0U ||
        stack_size < COXVBN_AARCH64_CONTEXT_BYTES ||
        (stack_size & 0xfU) != 0U ||
        stack_base > UINTPTR_MAX - stack_size)
        return -1;

    stack_top = stack_base + stack_size;
    frame_address = stack_top - COXVBN_AARCH64_CONTEXT_BYTES;
    frame_address &= ~(uintptr_t)0xfU;
    if (frame_address < stack_base ||
        frame_address > stack_top - COXVBN_AARCH64_CONTEXT_BYTES)
        return -1;

    storage = (uint64_t *)frame_address;
    for (i = 0U; i < COXVBN_AARCH64_CONTEXT_BYTES / sizeof(uint64_t); ++i)
        storage[i] = 0U;

    frame = (struct exception_frame *)frame_address;
    frame->x[0] = (uint64_t)argument;
    frame->x[30] = (uint64_t)return_trampoline;
    frame->elr = (uint64_t)entry;
    frame->spsr = COXVBN_AARCH64_SPSR_EL1H_MASKED;
    frame->vector = 0U;
    *frame_out = frame;
    return 0;
}
