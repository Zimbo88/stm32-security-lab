#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))

#define RCC_BASE      0x40023800UL
#define RCC_AHB1ENR   REG32(RCC_BASE + 0x30UL)
#define RCC_APB2ENR   REG32(RCC_BASE + 0x44UL)

#define GPIOA_BASE    0x40020000UL
#define GPIOH_BASE    0x40021C00UL
#define GPIO_MODER(b) REG32((b) + 0x00UL)
#define GPIO_OTYPER(b) REG32((b) + 0x04UL)
#define GPIO_OSPEEDR(b) REG32((b) + 0x08UL)
#define GPIO_PUPDR(b) REG32((b) + 0x0CUL)
#define GPIO_AFRH(b)  REG32((b) + 0x24UL)
#define GPIO_BSRR(b)  REG32((b) + 0x18UL)

#define USART1_BASE   0x40011000UL
#define USART_SR      REG32(USART1_BASE + 0x00UL)
#define USART_DR      REG32(USART1_BASE + 0x04UL)
#define USART_BRR     REG32(USART1_BASE + 0x08UL)
#define USART_CR1     REG32(USART1_BASE + 0x0CUL)
#define USART_CR2     REG32(USART1_BASE + 0x10UL)
#define USART_CR3     REG32(USART1_BASE + 0x14UL)

#define GPIOAEN       (1UL << 0)
#define GPIOHEN       (1UL << 7)
#define USART1EN      (1UL << 4)

#define USART_SR_TXE  (1UL << 7)
#define USART_CR1_UE  (1UL << 13)
#define USART_CR1_TE  (1UL << 3)
#define USART_CR1_RE  (1UL << 2)

#define PA9_PIN       9U
#define PA10_PIN      10U
#define PH12          (1UL << 12)

static void delay(volatile uint32_t n)
{
    while (n-- != 0U) {
        __asm volatile ("nop");
    }
}

static void uart_putc(char c)
{
    while ((USART_SR & USART_SR_TXE) == 0U) {
    }
    USART_DR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

static void gpio_init(void)
{
    RCC_AHB1ENR |= GPIOAEN | GPIOHEN;
    (void)RCC_AHB1ENR;

    /* PA9 and PA10: Alternate Function mode. */
    GPIO_MODER(GPIOA_BASE) &= ~((3UL << (PA9_PIN * 2U)) |
                               (3UL << (PA10_PIN * 2U)));
    GPIO_MODER(GPIOA_BASE) |=  ((2UL << (PA9_PIN * 2U)) |
                               (2UL << (PA10_PIN * 2U)));

    GPIO_OTYPER(GPIOA_BASE) &= ~((1UL << PA9_PIN) | (1UL << PA10_PIN));
    GPIO_OSPEEDR(GPIOA_BASE) |= ((2UL << (PA9_PIN * 2U)) |
                                 (2UL << (PA10_PIN * 2U)));
    GPIO_PUPDR(GPIOA_BASE) &= ~((3UL << (PA9_PIN * 2U)) |
                                (3UL << (PA10_PIN * 2U)));
    GPIO_PUPDR(GPIOA_BASE) |= (1UL << (PA10_PIN * 2U)); /* RX pull-up */

    /* AF7 on PA9 and PA10. */
    GPIO_AFRH(GPIOA_BASE) &= ~((0xFUL << ((PA9_PIN - 8U) * 4U)) |
                               (0xFUL << ((PA10_PIN - 8U) * 4U)));
    GPIO_AFRH(GPIOA_BASE) |=  ((7UL << ((PA9_PIN - 8U) * 4U)) |
                               (7UL << ((PA10_PIN - 8U) * 4U)));

    /* PH12 heartbeat output. */
    GPIO_MODER(GPIOH_BASE) &= ~(3UL << (12U * 2U));
    GPIO_MODER(GPIOH_BASE) |=  (1UL << (12U * 2U));
    GPIO_BSRR(GPIOH_BASE) = PH12 << 16U;
}

static void usart1_init(void)
{
    RCC_APB2ENR |= USART1EN;
    (void)RCC_APB2ENR;

    USART_CR1 = 0U;
    USART_CR2 = 0U;
    USART_CR3 = 0U;

    /*
     * Reset-default HSI = 16 MHz, APB2 = 16 MHz.
     * 115200 baud, oversampling by 16:
     * BRR = 0x008B, actual baud approximately 115107.
     */
    USART_BRR = 0x008BUL;
    USART_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

int main(void)
{
    gpio_init();
    usart1_init();

    uart_puts("\nSTM32F429 SECURITY LAB\n");
    uart_puts("EXP003 UART baseline\n");
    uart_puts("USART1: PA9 TX / PA10 RX, AF7, 115200 8N1\n");
    uart_puts("Clock: HSI 16 MHz\n");
    uart_puts("READY\n");

    for (;;) {
        GPIO_BSRR(GPIOH_BASE) = PH12;
        uart_puts("heartbeat 1\n");
        delay(4000000U);

        GPIO_BSRR(GPIOH_BASE) = PH12 << 16U;
        uart_puts("heartbeat 0\n");
        delay(4000000U);
    }
}
