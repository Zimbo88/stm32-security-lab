#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <stdint.h>

#include "byte_reader.h"

typedef enum {
    UART_OK = 0,
    UART_NO_DATA = 1,
    UART_TIMEOUT = 2,
    UART_ERR_INVALID_ARGUMENT = 3,
    UART_ERR_OVERRUN = 4,
    UART_ERR_FRAMING = 5,
    UART_ERR_NOISE = 6,
    UART_ERR_PARITY = 7
} uart_status_t;

void uart_init(void);
void uart_init_115200(uint32_t pclk_hz);
void uart_init_115200_hsi16(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_hex32(uint32_t value);
void uart_put_u32(uint32_t value);
uint8_t uart_rx_ready(void);
uart_status_t uart_getc_nonblocking(uint8_t *byte);
uart_status_t uart_getc_timeout(uint8_t *byte, uint32_t timeout_polls);
uart_status_t uart_read_timeout(
    uint8_t *buffer,
    size_t length,
    uint32_t per_byte_timeout_polls,
    size_t *bytes_read
);
uart_status_t uart_flush_rx(void);
void uart_byte_reader_init(byte_reader_t *reader);
const char *uart_status_text(uart_status_t status);

#ifdef UART_HOST_TEST
#define UART_HOST_USART_SR_PE    (1UL << 0)
#define UART_HOST_USART_SR_FE    (1UL << 1)
#define UART_HOST_USART_SR_NE    (1UL << 2)
#define UART_HOST_USART_SR_ORE   (1UL << 3)
#define UART_HOST_USART_SR_RXNE  (1UL << 5)
#define UART_HOST_USART_SR_TXE   (1UL << 7)
#define UART_HOST_USART_CR1_UE   (1UL << 13)
#define UART_HOST_USART_CR1_TE   (1UL << 3)
#define UART_HOST_USART_CR1_RE   (1UL << 2)

void uart_host_reset_registers(void);
void uart_host_set_usart_sr(uint32_t value);
void uart_host_set_usart_dr(uint32_t value);
uint32_t uart_host_get_rcc_ahb1enr(void);
uint32_t uart_host_get_rcc_apb2enr(void);
uint32_t uart_host_get_gpioa_moder(void);
uint32_t uart_host_get_gpioa_otyper(void);
uint32_t uart_host_get_gpioa_ospeedr(void);
uint32_t uart_host_get_gpioa_pupdr(void);
uint32_t uart_host_get_gpioa_afrh(void);
uint32_t uart_host_get_usart_sr(void);
uint32_t uart_host_get_usart_dr(void);
uint32_t uart_host_get_usart_brr(void);
uint32_t uart_host_get_usart_cr1(void);
uint32_t uart_host_get_usart_cr2(void);
uint32_t uart_host_get_usart_cr3(void);
#endif

#endif
