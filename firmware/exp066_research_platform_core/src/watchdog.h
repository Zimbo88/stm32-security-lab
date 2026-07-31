#ifndef PLATFORM_WATCHDOG_H
#define PLATFORM_WATCHDOG_H

#include <stdint.h>

/* Nominally about four seconds at a 32 kHz LSI. The STM32 LSI tolerance is
   deliberately documented and tested as a range, not hidden as a constant. */
#define PLATFORM_IWDG_PRESCALER_REGISTER 4U
#define PLATFORM_IWDG_RELOAD 2000U
#define PLATFORM_IWDG_TIMEOUT_NOMINAL_MS 4002UL
#define PLATFORM_IWDG_TIMEOUT_MIN_MS 2724UL
#define PLATFORM_IWDG_TIMEOUT_MAX_MS 7533UL

uint8_t platform_watchdog_init(void);
void platform_watchdog_refresh(void);
uint8_t platform_watchdog_active(void);

#endif
