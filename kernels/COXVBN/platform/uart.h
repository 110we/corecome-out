#ifndef COXVBN_UART_H
#define COXVBN_UART_H

void uart_init(void);
void uart_putc(char ch);
void uart_write(const char *text);

#endif
