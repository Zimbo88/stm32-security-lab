#ifndef PLATFORM_H
#define PLATFORM_H
#include <stddef.h>
#include <stdint.h>

#define PLATFORM_VERSION 1U
#define PLATFORM_ABI_VERSION 1U
#define PLATFORM_CLI_VERSION 1U
#define PLATFORM_BUILD_ID "exp066-1"
#define PLATFORM_FLASH_BASE 0x08000000UL
#define PLATFORM_FLASH_END 0x08100000UL
#define PLATFORM_APP_BASE 0x08008200UL
#define PLATFORM_SRAM_BASE 0x20000000UL
#define PLATFORM_SRAM_END 0x20020000UL

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
