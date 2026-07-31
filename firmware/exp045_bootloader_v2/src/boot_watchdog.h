#ifndef BOOT_WATCHDOG_H
#define BOOT_WATCHDOG_H

#include <stdint.h>

/* The application may leave IWDG running across a reset into Stage-0. */
#ifdef BOOT_FLASH_TARGET_HOST_TEST
static inline void boot_watchdog_refresh(void)
{
}
#else
void boot_watchdog_refresh(void);
#endif

#endif
