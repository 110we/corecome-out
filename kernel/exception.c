#include "exception.h"
#include "exception_decode.h"
#include "uart.h"

static const char *vector_name(uint64_t vector)
{
    static const char *const names[16] = {
        "CURRENT_SP0_SYNC", "CURRENT_SP0_IRQ",
        "CURRENT_SP0_FIQ", "CURRENT_SP0_SERROR",
        "LOWER_A64_SYNC", "LOWER_A64_IRQ",
        "LOWER_A64_FIQ", "LOWER_A64_SERROR",
        "LOWER_A32_SYNC", "LOWER_A32_IRQ",
        "LOWER_A32_FIQ", "LOWER_A32_SERROR",
        "CURRENT_SPX_SYNC", "CURRENT_SPX_IRQ",
        "CURRENT_SPX_FIQ", "CURRENT_SPX_SERROR"
    };
    return vector < 16U ? names[vector] : "INVALID_VECTOR";
}

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
        uart_write("\nCOXVBN: FATAL EXCEPTION\nVECTOR=");
        uart_write(vector_name(frame->vector));
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
