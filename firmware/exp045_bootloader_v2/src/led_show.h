#ifndef LED_SHOW_H
#define LED_SHOW_H

#include <stdint.h>

#define LED_SHOW_LED1 (1U << 0)
#define LED_SHOW_LED2 (1U << 1)
#define LED_SHOW_LED3 (1U << 2)
#define LED_SHOW_LED4 (1U << 3)
#define LED_SHOW_ALL  (LED_SHOW_LED1 | LED_SHOW_LED2 | \
                       LED_SHOW_LED3 | LED_SHOW_LED4)

void led_show_init(void);
void led_show_write(uint8_t led_mask);
void led_show_all_off(void);
_Noreturn void led_show_retro_loop(void);

#endif
