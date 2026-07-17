#include "platform_led.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define RCC_AHB1ENR REG32(0x40023830UL)
#define RCC_GPIOE_ENABLE (1UL << 4)
#define RCC_GPIOH_ENABLE (1UL << 7)

#define GPIO_MODER(base)   REG32((base) + 0x00UL)
#define GPIO_OTYPER(base)  REG32((base) + 0x04UL)
#define GPIO_OSPEEDR(base) REG32((base) + 0x08UL)
#define GPIO_PUPDR(base)   REG32((base) + 0x0CUL)
#define GPIO_BSRR(base)    REG32((base) + 0x18UL)

static void gpio_configure_output(uint32_t base, uint32_t pin)
{
    const uint32_t mode_shift = pin * 2U;
    const uint32_t mode_mask = 3UL << mode_shift;

    GPIO_MODER(base) &= ~mode_mask;
    GPIO_MODER(base) |= 1UL << mode_shift;
    GPIO_OTYPER(base) &= ~(1UL << pin);
    GPIO_OSPEEDR(base) &= ~mode_mask;
    GPIO_OSPEEDR(base) |= 1UL << mode_shift;
    GPIO_PUPDR(base) &= ~mode_mask;
}

static void gpio_write_pin(uint32_t base, uint32_t pin, uint8_t high)
{
    if (high != 0U) {
        GPIO_BSRR(base) = 1UL << pin;
    } else {
        GPIO_BSRR(base) = 1UL << (pin + 16U);
    }
}

static void led_write_physical(uint32_t base, uint32_t pin, uint8_t requested_on)
{
#if PLATFORM_LED_ACTIVE_LOW
    gpio_write_pin(base, pin, requested_on == 0U ? 1U : 0U);
#else
    gpio_write_pin(base, pin, requested_on);
#endif
}

void platform_led_init(void)
{
    RCC_AHB1ENR |= RCC_GPIOE_ENABLE | RCC_GPIOH_ENABLE;
    (void)RCC_AHB1ENR;

    gpio_configure_output(PLATFORM_LED1_GPIO_BASE, PLATFORM_LED1_GPIO_PIN);
    gpio_configure_output(PLATFORM_LED2_GPIO_BASE, PLATFORM_LED2_GPIO_PIN);
    gpio_configure_output(PLATFORM_LED3_GPIO_BASE, PLATFORM_LED3_GPIO_PIN);
    gpio_configure_output(PLATFORM_LED4_GPIO_BASE, PLATFORM_LED4_GPIO_PIN);

    platform_led_write(0U);
}

void platform_led_write(uint8_t mask)
{
    led_write_physical(
        PLATFORM_LED1_GPIO_BASE,
        PLATFORM_LED1_GPIO_PIN,
        (mask & PLATFORM_LED1) != 0U ? 1U : 0U
    );
    led_write_physical(
        PLATFORM_LED2_GPIO_BASE,
        PLATFORM_LED2_GPIO_PIN,
        (mask & PLATFORM_LED2) != 0U ? 1U : 0U
    );
    led_write_physical(
        PLATFORM_LED3_GPIO_BASE,
        PLATFORM_LED3_GPIO_PIN,
        (mask & PLATFORM_LED3) != 0U ? 1U : 0U
    );
    led_write_physical(
        PLATFORM_LED4_GPIO_BASE,
        PLATFORM_LED4_GPIO_PIN,
        (mask & PLATFORM_LED4) != 0U ? 1U : 0U
    );
}
