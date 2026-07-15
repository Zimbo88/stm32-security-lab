#include "uart.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define RCC_BASE      0x40023800UL
#define RCC_AHB1ENR   REG32(RCC_BASE + 0x30UL)
#define RCC_APB2ENR   REG32(RCC_BASE + 0x44UL)

#define GPIOA_BASE    0x40020000UL
#define GPIO_MODER    REG32(GPIOA_BASE + 0x00UL)
#define GPIO_OTYPER   REG32(GPIOA_BASE + 0x04UL)
#define GPIO_OSPEEDR  REG32(GPIOA_BASE + 0x08UL)
#define GPIO_PUPDR    REG32(GPIOA_BASE + 0x0CUL)
#define GPIO_AFRH     REG32(GPIOA_BASE + 0x24UL)

#define USART1_BASE   0x40011000UL
#define USART_SR      REG32(USART1_BASE + 0x00UL)
#define USART_DR      REG32(USART1_BASE + 0x04UL)
#define USART_BRR     REG32(USART1_BASE + 0x08UL)
#define USART_CR1     REG32(USART1_BASE + 0x0CUL)
#define USART_CR2     REG32(USART1_BASE + 0x10UL)
#define USART_CR3     REG32(USART1_BASE + 0x14UL)

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

    GPIO_MODER &= ~((3UL << 18) | (3UL << 20));
    GPIO_MODER |=  ((2UL << 18) | (2UL << 20));

    GPIO_OTYPER &= ~((1UL << 9) | (1UL << 10));
    GPIO_OSPEEDR |= ((2UL << 18) | (2UL << 20));

    GPIO_PUPDR &= ~((3UL << 18) | (3UL << 20));
    GPIO_PUPDR |=  (1UL << 20);

    GPIO_AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIO_AFRH |=  ((7UL << 4) | (7UL << 8));

    USART_CR1 = 0U;
    USART_CR2 = 0U;
    USART_CR3 = 0U;
    USART_BRR = 0x008BUL;
    USART_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(char c)
{
    while ((USART_SR & USART_SR_TXE) == 0U) {
    }
    USART_DR = (uint32_t)(uint8_t)c;
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
    static const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(hex[(value >> (uint32_t)shift) & 0xFUL]);
    }
}
