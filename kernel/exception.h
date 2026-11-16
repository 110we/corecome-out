#ifndef COXVBN_EXCEPTION_H
#define COXVBN_EXCEPTION_H

#include <stddef.h>
#include <stdint.h>

struct gicv2_state;

/* Must match the fixed offsets used by arch/aarch64/boot.S. */
struct exception_frame {
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
    uint64_t far;
    uint64_t vector;
};

typedef void (*exception_irq_handler)(uint32_t interrupt_id, void *context);

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
_Static_assert(offsetof(struct exception_frame, vector) == 280U,
               "exception vector offset mismatch");
_Static_assert(sizeof(struct exception_frame) == 288U,
               "exception frame payload size mismatch");

void exception_set_gicv2(struct gicv2_state *state);

/* Register/unregister only while IRQs are masked; handlers run in IRQ context. */
int exception_register_irq_handler(uint32_t interrupt_id,
                                   exception_irq_handler handler,
                                   void *context);
int exception_unregister_irq_handler(uint32_t interrupt_id);

/* Called by the vector assembly with a 16-byte-aligned saved frame. */
void exception_dispatch(struct exception_frame *frame);

#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void exception_panic(const struct exception_frame *frame);

#endif
