#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_init_115200_hsi16(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_hex32(uint32_t value);
void uart_put_u32(uint32_t value);

#endif
