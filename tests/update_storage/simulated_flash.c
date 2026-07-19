#include "simulated_flash.h"

#include <string.h>

static uint8_t range_to_offset(
    uint32_t address,
    size_t length,
    size_t *offset
)
{
    if ((offset == NULL) ||
        (address < STM32F429_FLASH_BASE) ||
        (length > STM32F429_FLASH_TOTAL_SIZE)) {
        return 0U;
    }

    const size_t start = (size_t)(address - STM32F429_FLASH_BASE);
    if (start > STM32F429_FLASH_TOTAL_SIZE ||
        length > (STM32F429_FLASH_TOTAL_SIZE - start)) {
        return 0U;
    }

    *offset = start;
    return 1U;
}

static boot_flash_status_t maybe_fail(simulated_flash_t *sim)
{
    sim->operation_count += 1U;
    if ((sim->fail_after_operation != 0U) &&
        (sim->operation_count == sim->fail_after_operation)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    return BOOT_FLASH_OK;
}

static void log_write(
    simulated_flash_t *sim,
    simulated_flash_write_kind_t kind,
    uint32_t address,
    uint32_t sector_id,
    size_t length
)
{
    if (sim->write_log_count >= SIMULATED_FLASH_WRITE_LOG_CAPACITY) {
        sim->write_log_overflow = 1U;
        return;
    }

    simulated_flash_write_log_entry_t *entry =
        &sim->write_log[sim->write_log_count];
    entry->kind = kind;
    entry->address = address;
    entry->sector_id = sector_id;
    entry->length = length;
    sim->write_log_count += 1U;
}

void simulated_flash_init(simulated_flash_t *sim)
{
    if (sim == NULL) {
        return;
    }

    memset(sim->storage, 0xFF, sizeof(sim->storage));
    sim->operation_count = 0U;
    sim->erase_count = 0U;
    sim->program_count = 0U;
    sim->read_count = 0U;
    sim->last_erase_sector = UINT32_MAX;
    sim->last_program_address = UINT32_MAX;
    sim->last_program_length = 0U;
    sim->fail_after_operation = 0U;
    sim->fail_before_erase_sector = UINT32_MAX;
    sim->fail_before_program_address = UINT32_MAX;
    sim->fail_before_read_address = UINT32_MAX;
    sim->corrupt_read_address = UINT32_MAX;
    sim->corrupt_after_program = 0U;
    sim->write_log_count = 0U;
    sim->write_log_overflow = 0U;
}

void simulated_flash_fail_after(simulated_flash_t *sim, uint32_t operation)
{
    if (sim != NULL) {
        sim->fail_after_operation = operation;
        sim->operation_count = 0U;
    }
}

void simulated_flash_fail_before_erase_sector(simulated_flash_t *sim, uint32_t sector_id)
{
    if (sim != NULL) {
        sim->fail_before_erase_sector = sector_id;
    }
}

void simulated_flash_fail_before_program_address(simulated_flash_t *sim, uint32_t address)
{
    if (sim != NULL) {
        sim->fail_before_program_address = address;
    }
}

void simulated_flash_fail_before_read_address(simulated_flash_t *sim, uint32_t address)
{
    if (sim != NULL) {
        sim->fail_before_read_address = address;
    }
}

void simulated_flash_corrupt_read_address(simulated_flash_t *sim, uint32_t address)
{
    if (sim != NULL) {
        sim->corrupt_read_address = address;
    }
}

void simulated_flash_corrupt_after_program(simulated_flash_t *sim, uint8_t enable)
{
    if (sim != NULL) {
        sim->corrupt_after_program = enable;
    }
}

uint8_t simulated_flash_peek(
    const simulated_flash_t *sim,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    size_t offset = 0U;

    if ((sim == NULL) ||
        ((length != 0U) && (output == NULL)) ||
        (range_to_offset(address, length, &offset) == 0U)) {
        return 0U;
    }

    if (length != 0U) {
        memcpy(output, &sim->storage[offset], length);
    }

    return 1U;
}

static boot_flash_status_t sim_read(
    void *context,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    simulated_flash_t *sim = (simulated_flash_t *)context;
    size_t offset = 0U;

    if ((sim == NULL) ||
        ((length != 0U) && (output == NULL)) ||
        (range_to_offset(address, length, &offset) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    sim->read_count += 1U;
    if (address == sim->fail_before_read_address) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    boot_flash_status_t status = maybe_fail(sim);
    if (status != BOOT_FLASH_OK) {
        return status;
    }

    if (length != 0U) {
        memcpy(output, &sim->storage[offset], length);
        if (address == sim->corrupt_read_address) {
            output[0] ^= 0x01U;
        }
    }

    return BOOT_FLASH_OK;
}

static boot_flash_status_t sim_erase_sector(void *context, uint32_t sector_id)
{
    simulated_flash_t *sim = (simulated_flash_t *)context;
    boot_flash_sector_t sector;
    size_t offset = 0U;

    if ((sim == NULL) ||
        (boot_flash_sector_by_id(sector_id, &sector) != BOOT_FLASH_OK) ||
        (range_to_offset(sector.base, sector.size, &offset) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    if (sector_id == sim->fail_before_erase_sector) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    log_write(
        sim,
        SIMULATED_FLASH_WRITE_ERASE,
        sector.base,
        sector_id,
        sector.size
    );
    memset(&sim->storage[offset], 0xFF, sector.size);
    sim->erase_count += 1U;
    sim->last_erase_sector = sector_id;
    return maybe_fail(sim);
}

static boot_flash_status_t sim_program(
    void *context,
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    simulated_flash_t *sim = (simulated_flash_t *)context;
    size_t offset = 0U;

    if ((sim == NULL) ||
        (data == NULL) ||
        (length == 0U) ||
        (range_to_offset(address, length, &offset) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    if (address == sim->fail_before_program_address) {
        return BOOT_FLASH_ERR_BACKEND;
    }

    for (size_t i = 0U; i < length; ++i) {
        if ((uint8_t)(sim->storage[offset + i] & data[i]) != data[i]) {
            return BOOT_FLASH_ERR_BACKEND;
        }
    }

    log_write(
        sim,
        SIMULATED_FLASH_WRITE_PROGRAM,
        address,
        UINT32_MAX,
        length
    );
    for (size_t i = 0U; i < length; ++i) {
        sim->storage[offset + i] = (uint8_t)(sim->storage[offset + i] & data[i]);
    }

    if (sim->corrupt_after_program != 0U) {
        sim->storage[offset] ^= 0x01U;
    }

    sim->program_count += 1U;
    sim->last_program_address = address;
    sim->last_program_length = length;
    return maybe_fail(sim);
}

const boot_flash_ops_t *simulated_flash_ops(void)
{
    static const boot_flash_ops_t ops = {
        .read = sim_read,
        .erase_sector = sim_erase_sector,
        .program = sim_program,
    };

    return &ops;
}
