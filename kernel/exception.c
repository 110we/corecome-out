#include "exception.h"
#include "uart.h"

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

void exception_panic(const struct exception_frame *frame)
{
    unsigned int i;
    if (frame == 0) {
        uart_write("\nCOXVBN: FATAL EXCEPTION; NO FRAME\n");
    } else {
        uart_write("\nCOXVBN: FATAL EXCEPTION\nELR=");
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
            {
                char digit = (char)('0' + (i / 10U));
                if (i >= 10U) uart_putc(digit);
                uart_putc((char)('0' + (i % 10U)));
            }
            uart_putc('=');
            write_hex(frame->x[i]);
            uart_write((i % 2U) == 1U ? "\n" : "  ");
        }
    }
    uart_write("COXVBN: HALTED AFTER EXCEPTION\n");
    for (;;) __asm__ volatile("wfe");
}
