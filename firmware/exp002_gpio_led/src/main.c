#include <stdint.h>

/*
 * EXP002: GPIO/LED baseline for the generic STM32F429IGT6 core board.
 *
 * Board labels observed on the photographs:
 *   PE3, PH10, PH11, PH12
 *
 * LED polarity is not assumed. Every output is toggled, so a connected LED
 * should visibly change state whether it is active-high or active-low.
 *
 * Clock: reset-default HSI, approximately 16 MHz.
 * No PLL, interrupts, external memory, USB, UART, or option-byte writes.
 */

#define REG32(address) (*(volatile uint32_t *)(address))

#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     REG32(RCC_BASE + 0x30UL)

#define GPIOE_BASE      0x40021000UL
#define GPIOH_BASE      0x40021C00UL

#define GPIO_MODER(base)  REG32((base) + 0x00UL)
#define GPIO_OTYPER(base) REG32((base) + 0x04UL)
#define GPIO_OSPEEDR(base) REG32((base) + 0x08UL)
#define GPIO_PUPDR(base)  REG32((base) + 0x0CUL)
#define GPIO_ODR(base)    REG32((base) + 0x14UL)
#define GPIO_BSRR(base)   REG32((base) + 0x18UL)

#define RCC_AHB1ENR_GPIOEEN (1UL << 4)
#define RCC_AHB1ENR_GPIOHEN (1UL << 7)

#define PE3   (1UL << 3)
#define PH10  (1UL << 10)
#define PH11  (1UL << 11)
#define PH12  (1UL << 12)
#define PH_LED_MASK (PH10 | PH11 | PH12)

static void delay_cycles(volatile uint32_t cycles)
{
    while (cycles-- != 0U) {
        __asm volatile ("nop");
    }
}

static void gpio_output_init(uint32_t base, uint32_t pin)
{
    uint32_t pin_number = 0U;
    uint32_t temp = pin;

    while ((temp >>= 1U) != 0U) {
        ++pin_number;
    }

    /* MODER = 01: general-purpose output. */
    GPIO_MODER(base) =
        (GPIO_MODER(base) & ~(3UL << (pin_number * 2U))) |
        (1UL << (pin_number * 2U));

    /* Push-pull, low speed, no pull-up/pull-down. */
    GPIO_OTYPER(base) &= ~pin;
    GPIO_OSPEEDR(base) &= ~(3UL << (pin_number * 2U));
    GPIO_PUPDR(base) &= ~(3UL << (pin_number * 2U));
}

static void toggle_pin(uint32_t base, uint32_t pin)
{
    if ((GPIO_ODR(base) & pin) != 0U) {
        GPIO_BSRR(base) = pin << 16U; /* reset output */
    } else {
        GPIO_BSRR(base) = pin;        /* set output */
    }
}

int main(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOHEN;
    (void)RCC_AHB1ENR; /* Ensure the peripheral-clock write has completed. */

    gpio_output_init(GPIOE_BASE, PE3);
    gpio_output_init(GPIOH_BASE, PH10);
    gpio_output_init(GPIOH_BASE, PH11);
    gpio_output_init(GPIOH_BASE, PH12);

    /* Known initial state: all four GPIO outputs low. */
    GPIO_BSRR(GPIOE_BASE) = PE3 << 16U;
    GPIO_BSRR(GPIOH_BASE) = PH_LED_MASK << 16U;

    for (;;) {
        toggle_pin(GPIOE_BASE, PE3);
        delay_cycles(2000000U);

        toggle_pin(GPIOH_BASE, PH10);
        delay_cycles(2000000U);

        toggle_pin(GPIOH_BASE, PH11);
        delay_cycles(2000000U);

        toggle_pin(GPIOH_BASE, PH12);
        delay_cycles(2000000U);
    }
}
