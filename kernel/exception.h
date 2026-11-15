#ifndef COXVBN_EXCEPTION_H
#define COXVBN_EXCEPTION_H

#include <stdint.h>

struct exception_frame {
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t far;
};

#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void exception_panic(const struct exception_frame *frame);

#endif
