#ifndef COXVBN_EXCEPTION_H
#define COXVBN_EXCEPTION_H

#include <stddef.h>
#include <stdint.h>

/* Must match the fixed offsets used by arch/aarch64/boot.S. */
struct exception_frame {
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t far;
};

_Static_assert(offsetof(struct exception_frame, x) == 0U,
               "exception x-register offset mismatch");
_Static_assert(offsetof(struct exception_frame, elr) == 248U,
               "exception ELR offset mismatch");
_Static_assert(offsetof(struct exception_frame, spsr) == 256U,
               "exception SPSR offset mismatch");
_Static_assert(offsetof(struct exception_frame, esr) == 264U,
               "exception ESR offset mismatch");
_Static_assert(offsetof(struct exception_frame, far) == 272U,
               "exception FAR offset mismatch");
_Static_assert(sizeof(struct exception_frame) == 280U,
               "exception frame payload size mismatch");

#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void exception_panic(const struct exception_frame *frame);

#endif
