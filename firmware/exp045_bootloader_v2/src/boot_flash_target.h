#ifndef BOOT_FLASH_TARGET_H
#define BOOT_FLASH_TARGET_H

#include "boot_flash.h"
#include "stm32f429_memory_layout.h"

#define BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT 1UL
#define BOOT_FLASH_TARGET_MAX_BUSY_POLLS 10000000UL

boot_flash_status_t boot_flash_target_init_disabled(boot_flash_t *flash);
boot_flash_status_t boot_flash_target_init_readonly(boot_flash_t *flash);
boot_flash_status_t boot_flash_target_init(boot_flash_t *flash);
boot_flash_status_t boot_flash_target_init_metadata(boot_flash_t *flash);
boot_flash_status_t boot_flash_target_init_lab_canary(
    boot_flash_t *flash,
    const boot_flash_region_t *regions,
    size_t region_count
);
uint8_t boot_flash_target_ramfunc_bounds_valid(void);

#ifdef BOOT_FLASH_TARGET_HOST_TEST
typedef struct {
    uint8_t storage[STM32F429_FLASH_TOTAL_SIZE];
    uint32_t acr;
    uint32_t sr;
    uint32_t cr;
    uint32_t keyr;
    uint32_t unlock_failure;
    uint32_t busy_timeout;
    uint32_t erase_error;
    uint32_t program_error;
    uint32_t lock_failure;
    uint32_t corrupt_after_program;
    uint32_t saved_primask;
    uint32_t restored_primask;
    uint32_t critical_enter_count;
    uint32_t critical_exit_count;
    uint32_t ramfunc_valid;
} boot_flash_target_host_context_t;

void boot_flash_target_host_context_init(boot_flash_target_host_context_t *context);
boot_flash_status_t boot_flash_target_init_host(
    boot_flash_t *flash,
    boot_flash_target_host_context_t *context,
    const boot_flash_region_t *write_regions,
    size_t write_region_count
);
#endif

#endif
