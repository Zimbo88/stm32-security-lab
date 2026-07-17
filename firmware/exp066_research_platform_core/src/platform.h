#ifndef PLATFORM_H
#define PLATFORM_H
#include <stddef.h>
#include <stdint.h>

#include "stm32f429_memory_layout.h"

#define PLATFORM_VERSION 1U
#define PLATFORM_ABI_VERSION 1U
#define PLATFORM_CLI_VERSION 1U
#define PLATFORM_BUILD_ID "exp066-1"
#define PLATFORM_FLASH_BASE STM32F429_FLASH_BANK1_BASE
#define PLATFORM_FLASH_END STM32F429_FLASH_BANK1_END
#define PLATFORM_APP_BASE STM32F429_APPLICATION_BASE
#define PLATFORM_SRAM_BASE STM32F429_MAIN_SRAM_BASE
#define PLATFORM_SRAM_END STM32F429_MAIN_SRAM_SUPPORTED_END

typedef enum { STATUS_OK = 0, STATUS_BAD_COMMAND = 1, STATUS_BAD_ARGUMENT = 2,
 STATUS_TOO_LONG = 3, STATUS_UNAVAILABLE = 4, STATUS_CORRUPT = 5 } status_t;
void platform_init(void);
uint32_t platform_millis(void);
void platform_tick(void);
void platform_idle(void);
void platform_cli_poll(void);
int fault_valid(void);
void fault_show(void);
void fault_clear(void);
#endif
