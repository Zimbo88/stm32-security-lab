#include "uart.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define RCC_AHB1ENR   REG32(0x40023830UL)
#define RCC_APB2ENR   REG32(0x40023844UL)

#define GPIOA_MODER   REG32(0x40020000UL)
#define GPIOA_OTYPER  REG32(0x40020004UL)
#define GPIOA_OSPEEDR REG32(0x40020008UL)
#define GPIOA_PUPDR   REG32(0x4002000CUL)
#define GPIOA_AFRH    REG32(0x40020024UL)

#define USART1_SR     REG32(0x40011000UL)
#define USART1_DR     REG32(0x40011004UL)
#define USART1_BRR    REG32(0x40011008UL)
#define USART1_CR1    REG32(0x4001100CUL)
#define USART1_CR2    REG32(0x40011010UL)
#define USART1_CR3    REG32(0x40011014UL)

#define USART_SR_TXE  (1UL << 7)
#define USART_CR1_UE  (1UL << 13)
#define USART_CR1_TE  (1UL << 3)
#define USART_CR1_RE  (1UL << 2)

void uart_init_115200_hsi16(void)
{
    RCC_AHB1ENR |= (1UL << 0);
    RCC_APB2ENR |= (1UL << 4);
    (void)RCC_AHB1ENR;
    (void)RCC_APB2ENR;

    GPIOA_MODER &= ~((3UL << 18) | (3UL << 20));
    GPIOA_MODER |=  ((2UL << 18) | (2UL << 20));

    GPIOA_OTYPER &= ~((1UL << 9) | (1UL << 10));
    GPIOA_OSPEEDR |= ((2UL << 18) | (2UL << 20));

    GPIOA_PUPDR &= ~((3UL << 18) | (3UL << 20));
    GPIOA_PUPDR |=  (1UL << 20);

    GPIOA_AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA_AFRH |=  ((7UL << 4) | (7UL << 8));

    USART1_CR1 = 0U;
    USART1_CR2 = 0U;
    USART1_CR3 = 0U;
    USART1_BRR = 0x008BUL;
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(char c)
{
    while ((USART1_SR & USART_SR_TXE) == 0U) {
    }
    USART1_DR = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

void uart_put_hex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> (uint32_t)shift) & 0xFUL]);
    }
}

void uart_put_u32(uint32_t value)
{
    char buffer[10];
    uint32_t count = 0U;

    if (value == 0U) {
        uart_putc('0');
        return;
    }

    while (value != 0U) {
        buffer[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (count != 0U) {
        uart_putc(buffer[--count]);
    }
}
