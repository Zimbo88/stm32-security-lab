#ifndef UART_H
#define UART_H
#include <stdint.h>
void uart_init(void);
void uart_putc(char value);
void uart_puts(const char *text);
void uart_put_hex32(uint32_t value);
void uart_put_u32(uint32_t value);
int uart_getc_nonblocking(char *value);
#endif
