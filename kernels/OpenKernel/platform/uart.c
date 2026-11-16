#include "uart.h"
#include <stdint.h>

#define PL011_BASE       0x09000000UL
#define UART_DR          0x000U
#define UART_FR          0x018U
#define UART_FR_TXFF     (1U << 5)

static volatile uint32_t *uart_reg(uintptr_t offset)
{
    return (volatile uint32_t *)(PL011_BASE + offset);
}

void openkernel_uart_init(void)
{
    /* QEMU virt firmware configures the PL011; keep initialization minimal. */
}

void openkernel_uart_putc(char value)
{
    while ((*uart_reg(UART_FR) & UART_FR_TXFF) != 0U) {
        __asm__ volatile("yield");
    }
    *uart_reg(UART_DR) = (uint32_t)(uint8_t)value;
}

void openkernel_uart_write(const char *text)
{
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        if (*text == '\n') {
            openkernel_uart_putc('\r');
        }
        openkernel_uart_putc(*text++);
    }
}
