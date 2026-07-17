#include "boot_flash_target.h"

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
