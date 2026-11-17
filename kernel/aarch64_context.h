#ifndef COXVBN_AARCH64_CONTEXT_H
#define COXVBN_AARCH64_CONTEXT_H

#include <stddef.h>
#include <stdint.h>
#include "exception.h"

#define COXVBN_AARCH64_CONTEXT_BYTES 304U
#define COXVBN_AARCH64_SPSR_EL1H_MASKED 0x3c5U

/*
 * Build the frame consumed by the common exception-return assembly.
 * The caller owns the stack and must keep it mapped and alive for the task.
 * This prepares a first-run frame; it does not switch SP or schedule a task.
 */
int coxvbn_aarch64_context_build(uintptr_t stack_base, size_t stack_size,
                                 uintptr_t entry, uintptr_t argument,
                                 uintptr_t return_trampoline,
                                 struct exception_frame **frame_out);

#endif
