#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

#define APP_BASE 0x08008000UL

uint32_t boot_application_is_valid(void);
void boot_jump_to_application(void);

#endif
