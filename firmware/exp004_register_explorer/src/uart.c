#include "uart.h"
#include "registers.h"

#define GPIOAEN         (1UL << 0)
#define USART1EN        (1UL << 4)

#define USART_SR_TXE    (1UL << 7)

#define USART_CR1_UE    (1UL << 13)
#define USART_CR1_TE    (1UL << 3)
#define USART_CR1_RE    (1UL << 2)

#define PA9             9U
#define PA10            10U

void uart_init(void)
{
    RCC_AHB1ENR |= GPIOAEN;
    RCC_APB2ENR |= USART1EN;

    (void)RCC_AHB1ENR;
    (void)RCC_APB2ENR;

    /* PA9 und PA10 in Alternate-Function-Modus. */
    GPIO_MODER(GPIOA_BASE) &=
        ~((3UL << (PA9 * 2U)) | (3UL << (PA10 * 2U)));

    GPIO_MODER(GPIOA_BASE) |=
        (2UL << (PA9 * 2U)) | (2UL << (PA10 * 2U));

    /* Push-pull. */
    GPIO_OTYPER(GPIOA_BASE) &=
        ~((1UL << PA9) | (1UL << PA10));

    /* Mittlere Geschwindigkeit. */
    GPIO_OSPEEDR(GPIOA_BASE) |=
        (2UL << (PA9 * 2U)) | (2UL << (PA10 * 2U));

    /* Kein Pull für TX, Pull-up für RX. */
    GPIO_PUPDR(GPIOA_BASE) &=
        ~((3UL << (PA9 * 2U)) | (3UL << (PA10 * 2U)));

    GPIO_PUPDR(GPIOA_BASE) |=
        (1UL << (PA10 * 2U));

    /* AF7 = USART1 auf PA9 und PA10. */
    GPIO_AFRH(GPIOA_BASE) &=
        ~((0xFUL << ((PA9 - 8U) * 4U)) |
          (0xFUL << ((PA10 - 8U) * 4U)));

    GPIO_AFRH(GPIOA_BASE) |=
        (7UL << ((PA9 - 8U) * 4U)) |
        (7UL << ((PA10 - 8U) * 4U));

    USART1_CR1 = 0U;
    USART1_CR2 = 0U;
    USART1_CR3 = 0U;

    /*
     * HSI = 16 MHz, APB2 = 16 MHz.
     * 115200 Baud, Oversampling 16.
     */
    USART1_BRR = 0x008BUL;

    USART1_CR1 = USART_CR1_UE |
                 USART_CR1_TE |
                 USART_CR1_RE;
}

void uart_putc(char c)
{
    while ((USART1_SR & USART_SR_TXE) == 0U) {
    }

    USART1_DR = (uint32_t)(uint8_t)c;
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

void uart_put_hex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    uart_puts("0x");

    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> (uint32_t)shift) & 0xFUL;
        uart_putc(digits[digit]);
    }
}
