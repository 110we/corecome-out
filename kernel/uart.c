#include "uart.h"

#include <stdint.h>

#define PL011_BASE 0x09000000UL
#define UART_DR    (*(volatile uint32_t *)(PL011_BASE + 0x00))
#define UART_FR    (*(volatile uint32_t *)(PL011_BASE + 0x18))
#define UART_FR_TXFF (1U << 5)

void uart_init(void)
{
    /* QEMU virt firmware configures the PL011 used as the primary console. */
}

void uart_putc(char ch)
{
    while ((UART_FR & UART_FR_TXFF) != 0U) {
        __asm__ volatile("yield");
    }
    UART_DR = (uint32_t)ch;
}

void uart_write(const char *text)
{
    if (text == 0) {
        return;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            uart_putc('\r');
        }
        uart_putc(*text++);
    }
}
