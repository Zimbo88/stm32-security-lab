#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(uint32_t pclk_hz, uint32_t baudrate);
void uart_putc(char c);
void uart_puts(const char *text);
void uart_put_hex32(uint32_t value);
void uart_put_u32(uint32_t value);

#endif
