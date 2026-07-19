#include "boot_flash.h"

#include <string.h>

#include "stm32f429_memory_layout.h"

#define BOOT_FLASH_VERIFY_CHUNK 32U

static const boot_flash_sector_t boot_flash_sectors[] = {
    {0U, 1U, STM32F429_FLASH_SECTOR_0_BASE, STM32F429_FLASH_SECTOR_0_SIZE, STM32F429_FLASH_SECTOR_0_END},
    {1U, 1U, STM32F429_FLASH_SECTOR_1_BASE, STM32F429_FLASH_SECTOR_1_SIZE, STM32F429_FLASH_SECTOR_1_END},
    {2U, 1U, STM32F429_FLASH_SECTOR_2_BASE, STM32F429_FLASH_SECTOR_2_SIZE, STM32F429_FLASH_SECTOR_2_END},
    {3U, 1U, STM32F429_FLASH_SECTOR_3_BASE, STM32F429_FLASH_SECTOR_3_SIZE, STM32F429_FLASH_SECTOR_3_END},
    {4U, 1U, STM32F429_FLASH_SECTOR_4_BASE, STM32F429_FLASH_SECTOR_4_SIZE, STM32F429_FLASH_SECTOR_4_END},
    {5U, 1U, STM32F429_FLASH_SECTOR_5_BASE, STM32F429_FLASH_SECTOR_5_SIZE, STM32F429_FLASH_SECTOR_5_END},
    {6U, 1U, STM32F429_FLASH_SECTOR_6_BASE, STM32F429_FLASH_SECTOR_6_SIZE, STM32F429_FLASH_SECTOR_6_END},
    {7U, 1U, STM32F429_FLASH_SECTOR_7_BASE, STM32F429_FLASH_SECTOR_7_SIZE, STM32F429_FLASH_SECTOR_7_END},
    {8U, 1U, STM32F429_FLASH_SECTOR_8_BASE, STM32F429_FLASH_SECTOR_8_SIZE, STM32F429_FLASH_SECTOR_8_END},
    {9U, 1U, STM32F429_FLASH_SECTOR_9_BASE, STM32F429_FLASH_SECTOR_9_SIZE, STM32F429_FLASH_SECTOR_9_END},
    {10U, 1U, STM32F429_FLASH_SECTOR_10_BASE, STM32F429_FLASH_SECTOR_10_SIZE, STM32F429_FLASH_SECTOR_10_END},
    {11U, 1U, STM32F429_FLASH_SECTOR_11_BASE, STM32F429_FLASH_SECTOR_11_SIZE, STM32F429_FLASH_SECTOR_11_END},
    {12U, 2U, STM32F429_FLASH_SECTOR_12_BASE, STM32F429_FLASH_SECTOR_12_SIZE, STM32F429_FLASH_SECTOR_12_END},
    {13U, 2U, STM32F429_FLASH_SECTOR_13_BASE, STM32F429_FLASH_SECTOR_13_SIZE, STM32F429_FLASH_SECTOR_13_END},
    {14U, 2U, STM32F429_FLASH_SECTOR_14_BASE, STM32F429_FLASH_SECTOR_14_SIZE, STM32F429_FLASH_SECTOR_14_END},
    {15U, 2U, STM32F429_FLASH_SECTOR_15_BASE, STM32F429_FLASH_SECTOR_15_SIZE, STM32F429_FLASH_SECTOR_15_END},
    {16U, 2U, STM32F429_FLASH_SECTOR_16_BASE, STM32F429_FLASH_SECTOR_16_SIZE, STM32F429_FLASH_SECTOR_16_END},
    {17U, 2U, STM32F429_FLASH_SECTOR_17_BASE, STM32F429_FLASH_SECTOR_17_SIZE, STM32F429_FLASH_SECTOR_17_END},
    {18U, 2U, STM32F429_FLASH_SECTOR_18_BASE, STM32F429_FLASH_SECTOR_18_SIZE, STM32F429_FLASH_SECTOR_18_END},
    {19U, 2U, STM32F429_FLASH_SECTOR_19_BASE, STM32F429_FLASH_SECTOR_19_SIZE, STM32F429_FLASH_SECTOR_19_END},
    {20U, 2U, STM32F429_FLASH_SECTOR_20_BASE, STM32F429_FLASH_SECTOR_20_SIZE, STM32F429_FLASH_SECTOR_20_END},
    {21U, 2U, STM32F429_FLASH_SECTOR_21_BASE, STM32F429_FLASH_SECTOR_21_SIZE, STM32F429_FLASH_SECTOR_21_END},
    {22U, 2U, STM32F429_FLASH_SECTOR_22_BASE, STM32F429_FLASH_SECTOR_22_SIZE, STM32F429_FLASH_SECTOR_22_END},
    {23U, 2U, STM32F429_FLASH_SECTOR_23_BASE, STM32F429_FLASH_SECTOR_23_SIZE, STM32F429_FLASH_SECTOR_23_END},
};

_Static_assert(
    sizeof(boot_flash_sectors) / sizeof(boot_flash_sectors[0]) ==
        STM32F429_FLASH_SECTOR_COUNT,
    "flash sector table must match generated layout"
);

static uint8_t checked_range_end(
    uint32_t address,
    size_t length,
    uint32_t *end
)
{
    if ((end == NULL) || (length > (size_t)UINT32_MAX)) {
        return 0U;
    }

    if (length > ((size_t)UINT32_MAX - (size_t)address)) {
        return 0U;
    }

    *end = address + (uint32_t)length;
    return 1U;
}

static uint8_t range_in_bounds(
    uint32_t base,
    uint32_t end,
    uint32_t address,
    size_t length
)
{
    uint32_t range_end = 0U;

    if (checked_range_end(address, length, &range_end) == 0U) {
        return 0U;
    }

    return ((address >= base) && (range_end <= end) && (range_end >= address))
        ? 1U
        : 0U;
}

static uint8_t range_is_write_allowed(
    const boot_flash_t *flash,
    uint32_t address,
    size_t length
)
{
    uint32_t range_end = 0U;

    if ((flash == NULL) ||
        (flash->write_regions == NULL) ||
        (flash->write_region_count == 0U) ||
        (checked_range_end(address, length, &range_end) == 0U)) {
        return 0U;
    }

    for (size_t i = 0U; i < flash->write_region_count; ++i) {
        const boot_flash_region_t *region = &flash->write_regions[i];
        if ((address >= region->base) && (range_end <= region->end)) {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t is_power_of_two(uint32_t value)
{
    return ((value != 0UL) && ((value & (value - 1UL)) == 0UL)) ? 1U : 0U;
}

boot_flash_status_t boot_flash_init(
    boot_flash_t *flash,
    void *context,
    const boot_flash_ops_t *ops,
    const boot_flash_region_t *write_regions,
    size_t write_region_count,
    uint32_t program_alignment
)
{
    if ((flash == NULL) ||
        (ops == NULL) ||
        (ops->read == NULL) ||
        (ops->erase_sector == NULL) ||
        (ops->program == NULL) ||
        (is_power_of_two(program_alignment) == 0U) ||
        ((write_region_count != 0U) && (write_regions == NULL))) {
        return BOOT_FLASH_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0U; i < write_region_count; ++i) {
        const boot_flash_region_t *region = &write_regions[i];
        if ((region->base >= region->end) ||
            (region->base < STM32F429_FLASH_BASE) ||
            (region->end > STM32F429_FLASH_END)) {
            return BOOT_FLASH_ERR_OUT_OF_BOUNDS;
        }
    }

    flash->context = context;
    flash->flash_base = STM32F429_FLASH_BASE;
    flash->flash_end = STM32F429_FLASH_END;
    flash->program_alignment = program_alignment;
    flash->write_regions = write_regions;
    flash->write_region_count = write_region_count;
    flash->ops = ops;
    return BOOT_FLASH_OK;
}

boot_flash_status_t boot_flash_sector_by_id(
    uint32_t sector_id,
    boot_flash_sector_t *sector
)
{
    if ((sector == NULL) ||
        (sector_id >= (sizeof(boot_flash_sectors) / sizeof(boot_flash_sectors[0])))) {
        return BOOT_FLASH_ERR_INVALID_ARGUMENT;
    }

    *sector = boot_flash_sectors[sector_id];
    return BOOT_FLASH_OK;
}

boot_flash_status_t boot_flash_bounds(
    const boot_flash_t *flash,
    uint32_t *base,
    uint32_t *end
)
{
    if ((flash == NULL) || (base == NULL) || (end == NULL)) {
        return BOOT_FLASH_ERR_INVALID_ARGUMENT;
    }

    *base = flash->flash_base;
    *end = flash->flash_end;
    return BOOT_FLASH_OK;
}

boot_flash_status_t boot_flash_read(
    const boot_flash_t *flash,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    if ((flash == NULL) ||
        (flash->ops == NULL) ||
        (flash->ops->read == NULL) ||
        ((length != 0U) && (output == NULL))) {
        return BOOT_FLASH_ERR_INVALID_ARGUMENT;
    }

    if (range_in_bounds(flash->flash_base, flash->flash_end, address, length) == 0U) {
        return BOOT_FLASH_ERR_OUT_OF_BOUNDS;
    }

    if (length == 0U) {
        return BOOT_FLASH_OK;
    }

    return flash->ops->read(flash->context, address, output, length);
}

boot_flash_status_t boot_flash_erase_sector(
    const boot_flash_t *flash,
    uint32_t sector_id
)
{
    boot_flash_sector_t sector;

    if ((flash == NULL) ||
        (flash->ops == NULL) ||
        (flash->ops->erase_sector == NULL)) {
        return BOOT_FLASH_ERR_INVALID_ARGUMENT;
    }

    boot_flash_status_t status = boot_flash_sector_by_id(sector_id, &sector);
    if (status != BOOT_FLASH_OK) {
        return status;
    }

    if (range_is_write_allowed(flash, sector.base, sector.size) == 0U) {
        return BOOT_FLASH_ERR_PROTECTED_REGION;
    }

    return flash->ops->erase_sector(flash->context, sector_id);
}

boot_flash_status_t boot_flash_program_aligned(
    const boot_flash_t *flash,
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    uint8_t verify[BOOT_FLASH_VERIFY_CHUNK];
    size_t offset = 0U;

    if ((flash == NULL) ||
        (flash->ops == NULL) ||
        (flash->ops->program == NULL) ||
        ((length != 0U) && (data == NULL))) {
        return BOOT_FLASH_ERR_INVALID_ARGUMENT;
    }

    if (length == 0U) {
        return BOOT_FLASH_OK;
    }

    if (((address & (flash->program_alignment - 1UL)) != 0UL) ||
        ((length & (size_t)(flash->program_alignment - 1UL)) != 0U)) {
        return BOOT_FLASH_ERR_ALIGNMENT;
    }

    if (range_in_bounds(flash->flash_base, flash->flash_end, address, length) == 0U) {
        return BOOT_FLASH_ERR_OUT_OF_BOUNDS;
    }

    if (range_is_write_allowed(flash, address, length) == 0U) {
        return BOOT_FLASH_ERR_PROTECTED_REGION;
    }

    boot_flash_status_t status =
        flash->ops->program(flash->context, address, data, length);
    if (status != BOOT_FLASH_OK) {
        return status;
    }

    while (offset < length) {
        size_t chunk = length - offset;
        if (chunk > sizeof(verify)) {
            chunk = sizeof(verify);
        }

        status = boot_flash_read(
            flash,
            address + (uint32_t)offset,
            verify,
            chunk
        );
        if (status != BOOT_FLASH_OK) {
            return status;
        }

        if (memcmp(verify, &data[offset], chunk) != 0) {
            return BOOT_FLASH_ERR_VERIFY;
        }

        offset += chunk;
    }

    return BOOT_FLASH_OK;
}

const char *boot_flash_status_text(boot_flash_status_t status)
{
    switch (status) {
    case BOOT_FLASH_OK:                   return "OK";
    case BOOT_FLASH_ERR_INVALID_ARGUMENT: return "INVALID ARGUMENT";
    case BOOT_FLASH_ERR_OUT_OF_BOUNDS:    return "OUT OF BOUNDS";
    case BOOT_FLASH_ERR_ALIGNMENT:        return "ALIGNMENT";
    case BOOT_FLASH_ERR_PROTECTED_REGION: return "PROTECTED REGION";
    case BOOT_FLASH_ERR_BACKEND:          return "BACKEND";
    case BOOT_FLASH_ERR_VERIFY:           return "VERIFY";
    default:                              return "UNKNOWN";
    }
}
