#ifndef LED_SHOW_H
#define LED_SHOW_H

#include <stdint.h>

#define LED_SHOW_LED1 (1U << 0)
#define LED_SHOW_LED2 (1U << 1)
#define LED_SHOW_LED3 (1U << 2)
#define LED_SHOW_LED4 (1U << 3)
#define LED_SHOW_ALL  (LED_SHOW_LED1 | LED_SHOW_LED2 | \
                       LED_SHOW_LED3 | LED_SHOW_LED4)

typedef enum {
    BOOT_LED_STATE_BOOTING = 0,
    BOOT_LED_STATE_VERIFYING = 1,
    BOOT_LED_STATE_RECOVERY = 2,
    BOOT_LED_STATE_FATAL = 3
} boot_led_state_t;

void led_show_init(void);
void led_show_write(uint8_t led_mask);
void led_show_all_off(void);
uint8_t led_show_state_mask(boot_led_state_t state, uint32_t phase);
uint8_t led_show_state_duration_units(boot_led_state_t state, uint32_t phase);
void led_show_indicate(boot_led_state_t state);
void led_show_signal(boot_led_state_t state);
_Noreturn void led_show_halt(boot_led_state_t state);
_Noreturn void led_show_retro_loop(void);

#endif
