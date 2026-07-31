#include "boot_watchdog.h"

#ifndef BOOT_FLASH_TARGET_HOST_TEST
#define IWDG_KR (*(volatile uint32_t *)0x40003000UL)
#define IWDG_KEY_REFRESH 0xAAAAUL

void boot_watchdog_refresh(void)
{
    IWDG_KR = IWDG_KEY_REFRESH;
}
#endif
