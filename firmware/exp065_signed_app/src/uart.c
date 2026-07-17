#include <stdint.h>

#include "uart.h"

#define REG32(address) (*(volatile uint32_t *)(address))

#define RCC_AHB1ENR REG32(0x40023830UL)
#define RCC_APB2ENR REG32(0x40023844UL)

#define GPIOA_MODER REG32(0x40020000UL)
#define GPIOA_AFRH  REG32(0x40020024UL)

#define USART1_SR   REG32(0x40011000UL)
#define USART1_DR   REG32(0x40011004UL)
#define USART1_BRR  REG32(0x40011008UL)
#define USART1_CR1  REG32(0x4001100CUL)
#define USART1_CR2  REG32(0x40011010UL)
#define USART1_CR3  REG32(0x40011014UL)

#define RCC_AHB1ENR_GPIOAEN (1UL << 0)
#define RCC_APB2ENR_USART1EN (1UL << 4)

#define USART_SR_TXE (1UL << 7)
#define USART_CR1_RE (1UL << 2)
#define USART_CR1_TE (1UL << 3)
#define USART_CR1_UE (1UL << 13)

static void uart_putc(char character)
{
    while ((USART1_SR & USART_SR_TXE) == 0U) {
    }

    USART1_DR = (uint32_t)(uint8_t)character;
}

void uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;

    /*
     * PA9  = USART1_TX, alternate function 7
     * PA10 = USART1_RX, alternate function 7
     */
    GPIOA_MODER &= ~((3UL << (9U * 2U)) | (3UL << (10U * 2U)));
    GPIOA_MODER |=  ((2UL << (9U * 2U)) | (2UL << (10U * 2U)));

    GPIOA_AFRH &= ~((0xFUL << 4U) | (0xFUL << 8U));
    GPIOA_AFRH |=  ((7UL << 4U) | (7UL << 8U));

    USART1_CR1 = 0U;
    USART1_CR2 = 0U;
    USART1_CR3 = 0U;

    /* 16 MHz peripheral clock, 115200 baud. */
    USART1_BRR = 0x008BUL;
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_puts(const char *text)
{
    while (*text != '\0') {
        if (*text == '\n') {
            uart_putc('\r');
        }

        uart_putc(*text);
        ++text;
    }
}
