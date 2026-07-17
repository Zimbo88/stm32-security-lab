#ifndef BOOT_FLASH_TARGET_H
#define BOOT_FLASH_TARGET_H

#include "boot_flash.h"

#define BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT 8UL

boot_flash_status_t boot_flash_target_init_disabled(boot_flash_t *flash);

#endif
