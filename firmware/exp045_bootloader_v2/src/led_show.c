#include "led_show.h"

#include "delay.h"

#include <stddef.h>

#define REG32(address) (*(volatile uint32_t *)(address))

#define RCC_AHB1ENR REG32(0x40023830UL)

#define GPIOE_BASE 0x40021000UL
#define GPIOH_BASE 0x40021C00UL

#define GPIO_MODER(base)   REG32((base) + 0x00UL)
#define GPIO_OTYPER(base)  REG32((base) + 0x04UL)
#define GPIO_OSPEEDR(base) REG32((base) + 0x08UL)
#define GPIO_PUPDR(base)   REG32((base) + 0x0CUL)
#define GPIO_BSRR(base)    REG32((base) + 0x18UL)

#define RCC_GPIOE_ENABLE (1UL << 4)
#define RCC_GPIOH_ENABLE (1UL << 7)

#define LED1_PIN 3U
#define LED2_PIN 10U
#define LED3_PIN 11U
#define LED4_PIN 12U

/*
 * Most variants of this STM32F429 core board use active-low LEDs:
 *
 * GPIO low  = LED on
 * GPIO high = LED off
 *
 * Change this value to 0U if the animation is visibly inverted.
 */
#define LED_SHOW_ACTIVE_LOW 1U

/*
 * Timing is based on the current 16 MHz HSI clock.
 * This is deliberately approximate: it creates a visual rhythm rather
 * than precise musical timing.
 */
#define LED_SHOW_BASE_DELAY_CYCLES 700000U

typedef struct {
    uint8_t mask;
    uint8_t duration;
} led_frame_t;

static const led_frame_t pattern_booting[] = {
    { LED_SHOW_LED4, 1U },
    { 0U,           1U }
};

static const led_frame_t pattern_verifying[] = {
    { LED_SHOW_LED2, 1U }
};

static const led_frame_t pattern_recovery[] = {
    { LED_SHOW_LED1, 1U },
    { 0U,           2U },
    { LED_SHOW_LED1, 1U },
    { 0U,           10U }
};

static const led_frame_t pattern_fatal[] = {
    { LED_SHOW_ALL, 1U },
    { 0U,           1U }
};

static const led_frame_t *frames_for_state(
    boot_led_state_t state,
    uint32_t *count
)
{
    if (count == NULL) {
        return pattern_fatal;
    }

    switch (state) {
    case BOOT_LED_STATE_BOOTING:
        *count = (uint32_t)(sizeof(pattern_booting) / sizeof(pattern_booting[0]));
        return pattern_booting;
    case BOOT_LED_STATE_VERIFYING:
        *count =
            (uint32_t)(sizeof(pattern_verifying) / sizeof(pattern_verifying[0]));
        return pattern_verifying;
    case BOOT_LED_STATE_RECOVERY:
        *count = (uint32_t)(sizeof(pattern_recovery) / sizeof(pattern_recovery[0]));
        return pattern_recovery;
    case BOOT_LED_STATE_FATAL:
    default:
        *count = (uint32_t)(sizeof(pattern_fatal) / sizeof(pattern_fatal[0]));
        return pattern_fatal;
    }
}

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

static void led_write_physical(
    uint32_t base,
    uint32_t pin,
    uint8_t requested_on
)
{
#if LED_SHOW_ACTIVE_LOW
    gpio_write_pin(base, pin, requested_on == 0U ? 1U : 0U);
#else
    gpio_write_pin(base, pin, requested_on);
#endif
}

static void led_show_wait(uint8_t units)
{
    uint8_t unit;

    for (unit = 0U; unit < units; ++unit) {
        delay_cycles(LED_SHOW_BASE_DELAY_CYCLES);
    }
}

static void led_show_play(
    const led_frame_t *frames,
    uint32_t frame_count
)
{
    uint32_t index;

    for (index = 0U; index < frame_count; ++index) {
        led_show_write(frames[index].mask);
        led_show_wait(frames[index].duration);
    }
}

void led_show_init(void)
{
    RCC_AHB1ENR |= RCC_GPIOE_ENABLE | RCC_GPIOH_ENABLE;

    /*
     * Complete the peripheral-clock enable before accessing GPIO registers.
     */
    (void)RCC_AHB1ENR;

    gpio_configure_output(GPIOE_BASE, LED1_PIN);
    gpio_configure_output(GPIOH_BASE, LED2_PIN);
    gpio_configure_output(GPIOH_BASE, LED3_PIN);
    gpio_configure_output(GPIOH_BASE, LED4_PIN);

    led_show_all_off();
}

void led_show_write(uint8_t led_mask)
{
    led_write_physical(
        GPIOE_BASE,
        LED1_PIN,
        (led_mask & LED_SHOW_LED1) != 0U ? 1U : 0U
    );

    led_write_physical(
        GPIOH_BASE,
        LED2_PIN,
        (led_mask & LED_SHOW_LED2) != 0U ? 1U : 0U
    );

    led_write_physical(
        GPIOH_BASE,
        LED3_PIN,
        (led_mask & LED_SHOW_LED3) != 0U ? 1U : 0U
    );

    led_write_physical(
        GPIOH_BASE,
        LED4_PIN,
        (led_mask & LED_SHOW_LED4) != 0U ? 1U : 0U
    );
}

void led_show_all_off(void)
{
    led_show_write(0U);
}

uint8_t led_show_state_mask(boot_led_state_t state, uint32_t phase)
{
    uint32_t count = 0U;
    const led_frame_t *frames = frames_for_state(state, &count);

    return frames[phase % count].mask;
}

uint8_t led_show_state_duration_units(boot_led_state_t state, uint32_t phase)
{
    uint32_t count = 0U;
    const led_frame_t *frames = frames_for_state(state, &count);

    return frames[phase % count].duration;
}

void led_show_indicate(boot_led_state_t state)
{
    led_show_write(led_show_state_mask(state, 0U));
}

void led_show_signal(boot_led_state_t state)
{
    uint32_t count = 0U;
    const led_frame_t *frames = frames_for_state(state, &count);

    led_show_play(frames, count);
    led_show_all_off();
}

_Noreturn void led_show_halt(boot_led_state_t state)
{
    uint32_t count = 0U;
    const led_frame_t *frames = frames_for_state(state, &count);

    led_show_init();

    for (;;) {
        led_show_play(frames, count);
    }
}

_Noreturn void led_show_retro_loop(void)
{
    led_show_halt(BOOT_LED_STATE_RECOVERY);
}
