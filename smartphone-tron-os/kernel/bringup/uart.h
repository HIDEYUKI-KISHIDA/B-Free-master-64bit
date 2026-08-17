#ifndef STOS_BRINGUP_UART_H
#define STOS_BRINGUP_UART_H

void stos_uart_init(void);
void stos_uart_putc(char c);
void stos_uart_puts(const char *s);

#endif
