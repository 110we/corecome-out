#ifndef CORECOME_UART_H
#define CORECOME_UART_H

void uart_init(void);
void uart_putc(char ch);
void uart_write(const char *text);

#endif
