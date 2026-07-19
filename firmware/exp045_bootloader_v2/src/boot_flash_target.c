#include "boot_flash_target.h"

#include <stdint.h>
#include <string.h>

#include "stm32f429_memory_layout.h"

static uint8_t target_range_is_flash(uint32_t address, size_t length)
{
    if ((address < STM32F429_FLASH_BASE) ||
        (address > STM32F429_FLASH_END) ||
        (length > STM32F429_FLASH_TOTAL_SIZE)) {
        return 0U;
    }

    const uint32_t max_length =
        STM32F429_FLASH_END - address;
    return (length <= (size_t)max_length) ? 1U : 0U;
}

static boot_flash_status_t target_disabled_read(
    void *context,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    (void)context;
    (void)address;
    (void)output;
    (void)length;
    return BOOT_FLASH_ERR_BACKEND;
}

static boot_flash_status_t target_readonly_read(
    void *context,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    (void)context;

    if (((length != 0U) && (output == NULL)) ||
        (target_range_is_flash(address, length) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    if (length != 0U) {
        memcpy(output, (const void *)(uintptr_t)address, length);
    }

    return BOOT_FLASH_OK;
}

static boot_flash_status_t target_disabled_erase_sector(
    void *context,
    uint32_t sector_id
)
{
    (void)context;
    (void)sector_id;
    return BOOT_FLASH_ERR_PROTECTED_REGION;
}

static boot_flash_status_t target_disabled_program(
    void *context,
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    (void)context;
    (void)address;
    (void)data;
    (void)length;
    return BOOT_FLASH_ERR_PROTECTED_REGION;
}

boot_flash_status_t boot_flash_target_init_disabled(boot_flash_t *flash)
{
    static const boot_flash_ops_t ops = {
        .read = target_disabled_read,
        .erase_sector = target_disabled_erase_sector,
        .program = target_disabled_program,
    };

    return boot_flash_init(
        flash,
        NULL,
        &ops,
        NULL,
        0U,
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
}

boot_flash_status_t boot_flash_target_init_readonly(boot_flash_t *flash)
{
    static const boot_flash_ops_t ops = {
        .read = target_readonly_read,
        .erase_sector = target_disabled_erase_sector,
        .program = target_disabled_program,
    };

    return boot_flash_init(
        flash,
        NULL,
        &ops,
        NULL,
        0U,
        BOOT_FLASH_TARGET_PROGRAM_ALIGNMENT
    );
}
