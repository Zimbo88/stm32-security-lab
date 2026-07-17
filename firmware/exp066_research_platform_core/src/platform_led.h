#ifndef PLATFORM_LED_H
#define PLATFORM_LED_H

#include <stdint.h>

#define PLATFORM_LED1 (1U << 0)
#define PLATFORM_LED2 (1U << 1)
#define PLATFORM_LED3 (1U << 2)
#define PLATFORM_LED4 (1U << 3)
#define PLATFORM_LED_ALL \
    (PLATFORM_LED1 | PLATFORM_LED2 | PLATFORM_LED3 | PLATFORM_LED4)

#define PLATFORM_LED_COUNT 4U

/*
 * Board-local LED pin definitions. The laboratory STM32F429IGT6 board notes
 * identify four active-low LEDs on PE3, PH10, PH11, and PH12. Keep polarity
 * and pin mapping centralized here; higher-level health code only writes
 * logical LED masks.
 */
#define PLATFORM_LED_ACTIVE_LOW 1U
#define PLATFORM_LED1_GPIO_BASE 0x40021000UL
#define PLATFORM_LED1_GPIO_PIN  3U
#define PLATFORM_LED2_GPIO_BASE 0x40021C00UL
#define PLATFORM_LED2_GPIO_PIN  10U
#define PLATFORM_LED3_GPIO_BASE 0x40021C00UL
#define PLATFORM_LED3_GPIO_PIN  11U
#define PLATFORM_LED4_GPIO_BASE 0x40021C00UL
#define PLATFORM_LED4_GPIO_PIN  12U

void platform_led_init(void);
void platform_led_write(uint8_t mask);

#endif
