#ifndef OPENKERNEL_UART_H
#define OPENKERNEL_UART_H

void openkernel_uart_init(void);
void openkernel_uart_putc(char value);
void openkernel_uart_write(const char *text);

#endif
