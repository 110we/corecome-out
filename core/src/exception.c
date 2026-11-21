#include <stdint.h>
#include "corecome/exception.h"
#include "corecome/uart.h"

static void uart_puthex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    for (shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> (unsigned)shift) & 0xfu]);
    }
}

void exception_handler(uint64_t kind, uint64_t esr, uint64_t far, uint64_t elr)
{
    static const char *const names[] = { "SYNC", "IRQ", "FIQ", "SERROR" };
    uart_puts("\n[FATAL] AArch64 exception: ");
    uart_puts(kind < 4u ? names[kind] : "UNKNOWN");
    uart_puts(" ESR_EL1=0x");
    uart_puthex(esr);
    uart_puts(" FAR_EL1=0x");
    uart_puthex(far);
    uart_puts(" ELR_EL1=0x");
    uart_puthex(elr);
    uart_puts("\n");
}