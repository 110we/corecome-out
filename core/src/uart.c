#include <stdint.h>
#include "corecome/uart.h"

#define UART0_BASE 0x09000000UL
#define UART_DR (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_FR_TXFF (1u << 5)

void uart_init(void)
{
    // QEMU virt firmware configures the PL011 clock.
}

void uart_putc(char c)
{
    while ((UART_FR & UART_FR_TXFF) != 0u) {
        __asm__ volatile("yield");
    }
    UART_DR = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s)
{
    if (s == 0) {
        return;
    }
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}
