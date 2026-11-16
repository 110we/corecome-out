#include "exception.h"
#include "exception_decode.h"
#include "gicv2.h"
#include "uart.h"

#define EXCEPTION_IRQ_SLOTS 1020U

struct irq_slot {
    exception_irq_handler handler;
    void *context;
};

static struct gicv2_state *active_gic;
static struct irq_slot irq_slots[EXCEPTION_IRQ_SLOTS];

static void write_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char out[16];
    unsigned int i;
    for (i = 0; i < 16U; ++i) {
        out[15U - i] = digits[value & 0xfU];
        value >>= 4;
    }
    uart_write("0x");
    for (i = 0; i < 16U; ++i) uart_putc(out[i]);
}

void exception_set_gicv2(struct gicv2_state *state)
{
    active_gic = state;
}

int exception_register_irq_handler(uint32_t interrupt_id,
                                   exception_irq_handler handler,
                                   void *context)
{
    if (active_gic == 0 || active_gic->initialized != 1U ||
        interrupt_id >= active_gic->interrupt_lines ||
        interrupt_id >= EXCEPTION_IRQ_SLOTS || handler == 0 ||
        irq_slots[interrupt_id].handler != 0)
        return -1;
    irq_slots[interrupt_id].context = context;
    irq_slots[interrupt_id].handler = handler;
    return 0;
}

int exception_unregister_irq_handler(uint32_t interrupt_id)
{
    if (interrupt_id >= EXCEPTION_IRQ_SLOTS ||
        irq_slots[interrupt_id].handler == 0)
        return -1;
    irq_slots[interrupt_id].handler = 0;
    irq_slots[interrupt_id].context = 0;
    return 0;
}

static void dispatch_irq(struct exception_frame *frame)
{
    uint32_t token;
    uint32_t interrupt_id;
    int rc;
    struct irq_slot *slot;

    if (active_gic == 0 || active_gic->initialized != 1U)
        exception_panic(frame);

    rc = gicv2_acknowledge(active_gic, &token);
    if (rc == GICV2_NO_INTERRUPT)
        return;
    if (rc != GICV2_INTERRUPT_PENDING)
        exception_panic(frame);

    interrupt_id = token & 0x3ffU;
    if (interrupt_id >= EXCEPTION_IRQ_SLOTS)
        exception_panic(frame);
    slot = &irq_slots[interrupt_id];
    if (slot->handler == 0) {
        if (gicv2_end_interrupt(active_gic, token) != GICV2_OK)
            exception_panic(frame);
        exception_panic(frame);
    }

    slot->handler(interrupt_id, slot->context);
    if (gicv2_end_interrupt(active_gic, token) != GICV2_OK)
        exception_panic(frame);
}

void exception_dispatch(struct exception_frame *frame)
{
    if (frame == 0)
        exception_panic(0);

    /* Current-EL SPx IRQ and lower-EL AArch64 IRQ vector slots. */
    if (frame->vector == 5U || frame->vector == 9U) {
        dispatch_irq(frame);
        return;
    }

    /* Synchronous, FIQ, SError, and unsupported-origin vectors fail closed. */
    exception_panic(frame);
}

void exception_panic(const struct exception_frame *frame)
{
    unsigned int i;
    if (frame == 0) {
        uart_write("\nCOXVBN: FATAL EXCEPTION; NO FRAME\n");
    } else {
        uart_write("\nCOXVBN: FATAL EXCEPTION\nVECTOR=");
        uart_write(exception_vector_name(frame->vector));
        uart_write(" INDEX=");
        write_hex(frame->vector);
        uart_write("\nCLASS=");
        uart_write(exception_class_name(frame->esr));
        uart_write("\nELR=");
        write_hex(frame->elr);
        uart_write(" SPSR=");
        write_hex(frame->spsr);
        uart_write("\nESR=");
        write_hex(frame->esr);
        uart_write(" FAR=");
        write_hex(frame->far);
        uart_write("\n");
        for (i = 0; i < 31U; ++i) {
            uart_write("x");
            if (i >= 10U) uart_putc((char)('0' + (i / 10U)));
            uart_putc((char)('0' + (i % 10U)));
            uart_putc('=');
            write_hex(frame->x[i]);
            uart_write((i % 2U) == 1U ? "\n" : "  ");
        }
    }
    uart_write("COXVBN: HALTED AFTER EXCEPTION\n");
    for (;;) {
#if defined(__aarch64__)
        __asm__ volatile("wfe");
#else
        __asm__ volatile("" ::: "memory");
#endif
    }
}
