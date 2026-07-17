#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOOT_FLASH_OK = 0,
    BOOT_FLASH_ERR_INVALID_ARGUMENT = 1,
    BOOT_FLASH_ERR_OUT_OF_BOUNDS = 2,
    BOOT_FLASH_ERR_ALIGNMENT = 3,
    BOOT_FLASH_ERR_PROTECTED_REGION = 4,
    BOOT_FLASH_ERR_BACKEND = 5,
    BOOT_FLASH_ERR_VERIFY = 6
} boot_flash_status_t;

typedef struct {
    uint32_t base;
    uint32_t end;
} boot_flash_region_t;

typedef struct {
    uint32_t id;
    uint32_t bank;
    uint32_t base;
    uint32_t size;
    uint32_t end;
} boot_flash_sector_t;

struct boot_flash;

typedef boot_flash_status_t (*boot_flash_read_fn_t)(
    void *context,
    uint32_t address,
    uint8_t *output,
    size_t length
);
typedef boot_flash_status_t (*boot_flash_erase_sector_fn_t)(
    void *context,
    uint32_t sector_id
);
typedef boot_flash_status_t (*boot_flash_program_fn_t)(
    void *context,
    uint32_t address,
    const uint8_t *data,
    size_t length
);

typedef struct {
    boot_flash_read_fn_t read;
    boot_flash_erase_sector_fn_t erase_sector;
    boot_flash_program_fn_t program;
} boot_flash_ops_t;

typedef struct boot_flash {
    void *context;
    uint32_t flash_base;
    uint32_t flash_end;
    uint32_t program_alignment;
    const boot_flash_region_t *write_regions;
    size_t write_region_count;
    const boot_flash_ops_t *ops;
} boot_flash_t;

boot_flash_status_t boot_flash_init(
    boot_flash_t *flash,
    void *context,
    const boot_flash_ops_t *ops,
    const boot_flash_region_t *write_regions,
    size_t write_region_count,
    uint32_t program_alignment
);
boot_flash_status_t boot_flash_sector_by_id(
    uint32_t sector_id,
    boot_flash_sector_t *sector
);
boot_flash_status_t boot_flash_bounds(
    const boot_flash_t *flash,
    uint32_t *base,
    uint32_t *end
);
boot_flash_status_t boot_flash_read(
    const boot_flash_t *flash,
    uint32_t address,
    uint8_t *output,
    size_t length
);
boot_flash_status_t boot_flash_erase_sector(
    const boot_flash_t *flash,
    uint32_t sector_id
);
boot_flash_status_t boot_flash_program_aligned(
    const boot_flash_t *flash,
    uint32_t address,
    const uint8_t *data,
    size_t length
);
const char *boot_flash_status_text(boot_flash_status_t status);

#endif
