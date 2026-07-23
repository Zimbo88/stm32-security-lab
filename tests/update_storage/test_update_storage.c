#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "boot_flash.h"
#include "boot_flash_target.h"
#include "boot_confirmation.h"
#include "boot_metadata.h"
#include "boot_metadata_provision_core.h"
#include "boot_slot.h"
#include "boot_slot_selection.h"
#include "monocypher-ed25519.h"
#include "simulated_flash.h"
#include "stm32f429_memory_layout.h"
#include "update_installer.h"
#include "update_package.h"

static int failures;

#define TEST_METADATA_BODY_SIZE 60U
#define TEST_METADATA_CRC_OFFSET 60U
#define TEST_METADATA_PADDING_OFFSET 64U
#define TEST_METADATA_COMMIT_OFFSET (STM32F429_BOOT_METADATA_RECORD_SIZE - 8U)
#define TEST_UPDATE_PAYLOAD_SIZE 256U
#define TEST_INSTALL_PROGRAM_CHUNK 128U
#define TEST_SMALL_INSTALL_CHUNK 512U
#define TEST_LARGE_UPDATE_PAYLOAD_SIZE 2048U
#define TEST_PACKAGE_BUFFER_SIZE \
    (STM32F429_SIGNED_IMAGE_HEADER_SIZE + TEST_UPDATE_PAYLOAD_SIZE + 32U)
#define TEST_LARGE_PACKAGE_BUFFER_SIZE \
    (STM32F429_SIGNED_IMAGE_HEADER_SIZE + TEST_LARGE_UPDATE_PAYLOAD_SIZE + 32U)

static const boot_flash_region_t write_regions[] = {
    {STM32F429_BOOT_METADATA_A_BASE, STM32F429_BOOT_METADATA_A_END},
    {STM32F429_BOOT_METADATA_B_BASE, STM32F429_BOOT_METADATA_B_END},
    {STM32F429_UPDATE_METADATA_BASE, STM32F429_UPDATE_METADATA_END},
    {STM32F429_SLOT_B_SIGNED_IMAGE_BASE, STM32F429_SLOT_B_END},
};

static const boot_flash_region_t installer_write_regions[] = {
    {STM32F429_BOOT_METADATA_A_BASE, STM32F429_BOOT_METADATA_A_END},
    {STM32F429_BOOT_METADATA_B_BASE, STM32F429_BOOT_METADATA_B_END},
    {STM32F429_SLOT_A_SIGNED_IMAGE_BASE, STM32F429_SLOT_A_END},
    {STM32F429_SLOT_B_SIGNED_IMAGE_BASE, STM32F429_SLOT_B_END},
};

static uint8_t update_secret_key[64];
static uint8_t update_public_key[FIRMWARE_PUBLIC_KEY_SIZE];
static uint8_t protected_stage0_before[STM32F429_BOOTLOADER_SIZE];
static uint8_t protected_slot_before[STM32F429_SLOT_A_SIZE];
static uint8_t protected_recovery_before[STM32F429_RECOVERY_SIZE];

static void expect_status(
    const char *name,
    boot_flash_status_t expected,
    boot_flash_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            boot_flash_status_text(expected),
            (int)expected,
            boot_flash_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
    if (actual != expected) {
        printf(
            "%s: expected 0x%08lx, got 0x%08lx\n",
            name,
            (unsigned long)expected,
            (unsigned long)actual
        );
        failures += 1;
    }
}

static uint32_t test_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    crc = ~crc;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0UL - (crc & 1UL);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static void test_store_le32(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    out[2] = (uint8_t)((value >> 16) & 0xFFU);
    out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void refresh_metadata_crc(uint8_t record[STM32F429_BOOT_METADATA_RECORD_SIZE])
{
    test_store_le32(
        &record[TEST_METADATA_CRC_OFFSET],
        test_crc32_update(0UL, record, TEST_METADATA_BODY_SIZE)
    );
}

static void expect_metadata_status(
    const char *name,
    boot_metadata_status_t expected,
    boot_metadata_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            boot_metadata_status_text(expected),
            (int)expected,
            boot_metadata_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void expect_package_status(
    const char *name,
    update_package_status_t expected,
    update_package_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            update_package_status_text(expected),
            (int)expected,
            update_package_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void expect_install_status(
    const char *name,
    update_install_status_t expected,
    update_install_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            update_install_status_text(expected),
            (int)expected,
            update_install_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void expect_selection_status(
    const char *name,
    boot_slot_selection_status_t expected,
    boot_slot_selection_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            boot_slot_selection_status_text(expected),
            (int)expected,
            boot_slot_selection_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void expect_confirm_status(
    const char *name,
    boot_confirm_status_t expected,
    boot_confirm_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            boot_confirm_status_text(expected),
            (int)expected,
            boot_confirm_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void expect_verify_status(
    const char *name,
    verify_status_t expected,
    verify_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            signed_image_status_text(expected),
            (int)expected,
            signed_image_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void expect_metadata_record(
    const char *name,
    const boot_metadata_record_t *record,
    uint32_t sequence,
    boot_metadata_state_t state,
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t version
)
{
    if (record == NULL) {
        printf("%s: null record\n", name);
        failures += 1;
        return;
    }

    expect_u32(name, sequence, record->sequence);
    if (record->state != state) {
        printf(
            "%s state: expected %s, got %s\n",
            name,
            boot_metadata_state_text(state),
            boot_metadata_state_text(record->state)
        );
        failures += 1;
    }
    expect_u32("active slot", active_slot, record->active_slot);
    expect_u32("candidate slot", candidate_slot, record->candidate_slot);
    expect_u32("candidate version", version, record->candidate_image_version);
}

static void make_flash(simulated_flash_t *sim, boot_flash_t *flash)
{
    simulated_flash_init(sim);
    expect_status(
        "flash init",
        BOOT_FLASH_OK,
        boot_flash_init(
            flash,
            sim,
            simulated_flash_ops(),
            write_regions,
            sizeof(write_regions) / sizeof(write_regions[0]),
            8U
        )
    );
}

static void make_installer_flash(simulated_flash_t *sim, boot_flash_t *flash)
{
    simulated_flash_init(sim);
    expect_status(
        "installer flash init",
        BOOT_FLASH_OK,
        boot_flash_init(
            flash,
            sim,
            simulated_flash_ops(),
            installer_write_regions,
            sizeof(installer_write_regions) / sizeof(installer_write_regions[0]),
            8U
        )
    );
}

static size_t flash_offset(uint32_t address)
{
    return (size_t)(address - STM32F429_FLASH_BASE);
}

static void fill_flash_region(
    simulated_flash_t *sim,
    uint32_t address,
    size_t length,
    uint8_t seed
)
{
    const size_t offset = flash_offset(address);

    for (size_t i = 0U; i < length; ++i) {
        sim->storage[offset + i] = (uint8_t)(seed + (uint8_t)i);
    }
}

static void snapshot_flash_region(
    const simulated_flash_t *sim,
    uint32_t address,
    uint8_t *snapshot,
    size_t length
)
{
    memcpy(snapshot, &sim->storage[flash_offset(address)], length);
}

static void expect_flash_region_unchanged(
    const char *name,
    const simulated_flash_t *sim,
    uint32_t address,
    const uint8_t *snapshot,
    size_t length
)
{
    if (memcmp(&sim->storage[flash_offset(address)], snapshot, length) != 0) {
        printf("%s changed unexpectedly\n", name);
        failures += 1;
    }
}

static void prepare_protected_snapshots(
    simulated_flash_t *sim,
    const boot_slot_descriptor_t *active
)
{
    fill_flash_region(
        sim,
        STM32F429_BOOTLOADER_BASE,
        STM32F429_BOOTLOADER_SIZE,
        0x10U
    );
    fill_flash_region(sim, active->signed_image_base, STM32F429_SLOT_A_SIZE, 0x40U);
    fill_flash_region(sim, STM32F429_RECOVERY_BASE, STM32F429_RECOVERY_SIZE, 0x80U);

    snapshot_flash_region(
        sim,
        STM32F429_BOOTLOADER_BASE,
        protected_stage0_before,
        sizeof(protected_stage0_before)
    );
    snapshot_flash_region(
        sim,
        active->signed_image_base,
        protected_slot_before,
        sizeof(protected_slot_before)
    );
    snapshot_flash_region(
        sim,
        STM32F429_RECOVERY_BASE,
        protected_recovery_before,
        sizeof(protected_recovery_before)
    );
}

static void expect_protected_regions_unchanged(
    const simulated_flash_t *sim,
    const boot_slot_descriptor_t *active
)
{
    expect_flash_region_unchanged(
        "Stage 0",
        sim,
        STM32F429_BOOTLOADER_BASE,
        protected_stage0_before,
        sizeof(protected_stage0_before)
    );
    expect_flash_region_unchanged(
        "active slot",
        sim,
        active->signed_image_base,
        protected_slot_before,
        sizeof(protected_slot_before)
    );
    expect_flash_region_unchanged(
        "recovery region",
        sim,
        STM32F429_RECOVERY_BASE,
        protected_recovery_before,
        sizeof(protected_recovery_before)
    );
}

static uint32_t count_writes_in_range(
    const simulated_flash_t *sim,
    uint32_t base,
    uint32_t end
)
{
    uint32_t count = 0U;

    for (size_t i = 0U; i < sim->write_log_count; ++i) {
        const simulated_flash_write_log_entry_t *entry = &sim->write_log[i];
        const uint32_t entry_end =
            entry->address + (uint32_t)entry->length;
        if ((entry->address < end) && (entry_end > base)) {
            count += 1U;
        }
    }

    return count;
}

static void test_sector_lookup_and_bounds(void)
{
    boot_flash_sector_t sector;
    simulated_flash_t sim;
    boot_flash_t flash;
    uint32_t base = 0U;
    uint32_t end = 0U;

    make_flash(&sim, &flash);
    expect_status("bounds", BOOT_FLASH_OK, boot_flash_bounds(&flash, &base, &end));
    expect_u32("flash base", STM32F429_FLASH_BASE, base);
    expect_u32("flash end", STM32F429_FLASH_END, end);

    expect_status("sector 0", BOOT_FLASH_OK, boot_flash_sector_by_id(0U, &sector));
    expect_u32("sector 0 base", 0x08000000UL, sector.base);
    expect_u32("sector 0 end", 0x08004000UL, sector.end);

    expect_status("sector 5", BOOT_FLASH_OK, boot_flash_sector_by_id(5U, &sector));
    expect_u32("sector 5 base", STM32F429_SLOT_A_SIGNED_IMAGE_BASE, sector.base);
    expect_u32("sector 5 size", 0x00020000UL, sector.size);

    expect_status("sector 8", BOOT_FLASH_OK, boot_flash_sector_by_id(8U, &sector));
    expect_u32("sector 8 base", STM32F429_SLOT_B_SIGNED_IMAGE_BASE, sector.base);
    expect_u32("sector 8 size", 0x00020000UL, sector.size);

    expect_status("sector 11", BOOT_FLASH_OK, boot_flash_sector_by_id(11U, &sector));
    expect_u32("sector 11 base", STM32F429_RECOVERY_BASE, sector.base);
    expect_u32("sector 11 end", STM32F429_FLASH_END, sector.end);

    expect_status(
        "invalid sector",
        BOOT_FLASH_ERR_INVALID_ARGUMENT,
        boot_flash_sector_by_id(12U, &sector)
    );
    for (uint32_t invalid_sector = 12U; invalid_sector <= 23U; ++invalid_sector) {
        expect_status(
            "non-existent 1 MiB sector rejected",
            BOOT_FLASH_ERR_INVALID_ARGUMENT,
            boot_flash_sector_by_id(invalid_sector, &sector)
        );
    }
}

static void test_allowed_erase_policy(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;

    make_flash(&sim, &flash);

    expect_status(
        "erase metadata A",
        BOOT_FLASH_OK,
        boot_flash_erase_sector(&flash, STM32F429_BOOT_METADATA_A_FIRST_SECTOR)
    );
    expect_status(
        "erase update metadata",
        BOOT_FLASH_OK,
        boot_flash_erase_sector(&flash, STM32F429_UPDATE_METADATA_FIRST_SECTOR)
    );
    expect_status(
        "erase inactive slot",
        BOOT_FLASH_OK,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_B_FIRST_SECTOR)
    );
    expect_status(
        "erase stage0 rejected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_erase_sector(&flash, STM32F429_BOOTLOADER_FIRST_SECTOR)
    );
    expect_status(
        "erase active slot rejected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_A_FIRST_SECTOR)
    );
    expect_status(
        "erase recovery rejected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_erase_sector(&flash, STM32F429_RECOVERY_FIRST_SECTOR)
    );
    expect_u32("only allowed erases reached backend", 3U, sim.erase_count);
    expect_u32("last erase was inactive slot", STM32F429_SLOT_B_FIRST_SECTOR, sim.last_erase_sector);
}

static void test_target_flash_stub_fails_closed(void)
{
    boot_flash_t flash;
    uint8_t data[8] = {0U};
    uint8_t output[8];

    expect_status(
        "target stub init",
        BOOT_FLASH_OK,
        boot_flash_target_init_disabled(&flash)
    );
    expect_status(
        "target stub read disabled",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_read(&flash, STM32F429_FLASH_BASE, output, sizeof(output))
    );
    expect_status(
        "target stub erase protected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_erase_sector(&flash, STM32F429_BOOT_METADATA_A_FIRST_SECTOR)
    );
    expect_status(
        "target stub program protected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_program_aligned(&flash, STM32F429_BOOT_METADATA_A_BASE, data, sizeof(data))
    );
}

static void make_target_host_flash(
    boot_flash_target_host_context_t *target,
    boot_flash_t *flash,
    const boot_flash_region_t *regions,
    size_t region_count
)
{
    boot_flash_target_host_context_init(target);
    expect_status(
        "target host flash init",
        BOOT_FLASH_OK,
        boot_flash_target_init_host(flash, target, regions, region_count)
    );
}

static void target_snapshot(
    const boot_flash_target_host_context_t *target,
    uint32_t address,
    uint8_t *snapshot,
    size_t length
)
{
    memcpy(snapshot, &target->storage[flash_offset(address)], length);
}

static void expect_target_region_unchanged(
    const char *name,
    const boot_flash_target_host_context_t *target,
    uint32_t address,
    const uint8_t *snapshot,
    size_t length
)
{
    if (memcmp(&target->storage[flash_offset(address)], snapshot, length) != 0) {
        printf("%s changed unexpectedly\n", name);
        failures += 1;
    }
}

static void test_target_flash_backend_model_success_and_policy(void)
{
    boot_flash_target_host_context_t target;
    boot_flash_t flash;
    uint8_t data[4] = {0x12U, 0x34U, 0x56U, 0x78U};
    uint8_t readback[4] = {0U};
    uint8_t stage0_before[16];
    uint8_t recovery_before[16];

    make_target_host_flash(
        &target,
        &flash,
        write_regions,
        sizeof(write_regions) / sizeof(write_regions[0])
    );

    for (size_t i = 0U; i < sizeof(stage0_before); ++i) {
        target.storage[flash_offset(STM32F429_BOOTLOADER_BASE) + i] =
            (uint8_t)(0x20U + i);
        target.storage[flash_offset(STM32F429_RECOVERY_BASE) + i] =
            (uint8_t)(0x80U + i);
    }
    target_snapshot(&target, STM32F429_BOOTLOADER_BASE, stage0_before, sizeof(stage0_before));
    target_snapshot(&target, STM32F429_RECOVERY_BASE, recovery_before, sizeof(recovery_before));

    expect_status(
        "target host zero program no-op",
        BOOT_FLASH_OK,
        boot_flash_program_aligned(&flash, STM32F429_SLOT_B_SIGNED_IMAGE_BASE, NULL, 0U)
    );
    expect_status(
        "target host erase slot B",
        BOOT_FLASH_OK,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_B_FIRST_SECTOR)
    );
    expect_status(
        "target host program slot B",
        BOOT_FLASH_OK,
        boot_flash_program_aligned(
            &flash,
            STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
            data,
            sizeof(data)
        )
    );
    expect_status(
        "target host read slot B",
        BOOT_FLASH_OK,
        boot_flash_read(&flash, STM32F429_SLOT_B_SIGNED_IMAGE_BASE, readback, sizeof(readback))
    );
    if (memcmp(data, readback, sizeof(data)) != 0) {
        printf("target host readback mismatch\n");
        failures += 1;
    }
    expect_u32("target relocked", (1UL << 31), target.cr & (1UL << 31));
    expect_u32("interrupt enter count", 2U, target.critical_enter_count);
    expect_u32("interrupt exit count", 2U, target.critical_exit_count);
    expect_u32("interrupt restored", 0U, target.restored_primask);
    expect_target_region_unchanged(
        "target Stage 0",
        &target,
        STM32F429_BOOTLOADER_BASE,
        stage0_before,
        sizeof(stage0_before)
    );
    expect_target_region_unchanged(
        "target recovery",
        &target,
        STM32F429_RECOVERY_BASE,
        recovery_before,
        sizeof(recovery_before)
    );

    expect_status(
        "target host protected Stage 0 erase",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_erase_sector(&flash, STM32F429_BOOTLOADER_FIRST_SECTOR)
    );
    expect_status(
        "target host protected recovery erase",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_erase_sector(&flash, STM32F429_RECOVERY_FIRST_SECTOR)
    );
    expect_status(
        "target host spanning boundary rejected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_program_aligned(&flash, STM32F429_SLOT_B_END - 1UL, data, 2U)
    );
}

static void test_target_flash_backend_error_mapping(void)
{
    const boot_flash_region_t only_slot_b[] = {
        {STM32F429_SLOT_B_SIGNED_IMAGE_BASE, STM32F429_SLOT_B_END},
    };
    boot_flash_target_host_context_t target;
    boot_flash_t flash;
    uint8_t data[4] = {0xAAU, 0x55U, 0x11U, 0x22U};

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    for (uint32_t invalid_sector = 12U; invalid_sector <= 23U; ++invalid_sector) {
        expect_status(
            "target non-existent sector rejected",
            BOOT_FLASH_ERR_INVALID_ARGUMENT,
            boot_flash_erase_sector(&flash, invalid_sector)
        );
    }
    expect_u32("invalid sectors did not unlock", (1UL << 31), target.cr & (1UL << 31));
    expect_u32("invalid sectors no critical enter", 0U, target.critical_enter_count);

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    target.unlock_failure = 1U;
    expect_status(
        "target unlock failure",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_B_FIRST_SECTOR)
    );
    expect_u32("unlock failure kept lock", (1UL << 31), target.cr & (1UL << 31));
    expect_u32("unlock failure no critical enter", 0U, target.critical_enter_count);

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    target.busy_timeout = 1U;
    expect_status(
        "target busy timeout",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_B_FIRST_SECTOR)
    );
    expect_u32("busy timeout restored interrupt", 1U, target.critical_exit_count);
    expect_u32("busy timeout relocked", (1UL << 31), target.cr & (1UL << 31));

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    target.erase_error = 1U;
    expect_status(
        "target erase error",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_B_FIRST_SECTOR)
    );
    expect_u32("erase error relocked", (1UL << 31), target.cr & (1UL << 31));

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    target.program_error = 1U;
    expect_status(
        "target program error",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_program_aligned(
            &flash,
            STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
            data,
            sizeof(data)
        )
    );
    expect_u32("program error relocked", (1UL << 31), target.cr & (1UL << 31));

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    target.lock_failure = 1U;
    expect_status(
        "target lock failure",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_B_FIRST_SECTOR)
    );
    expect_u32("lock failure visible", 0U, target.cr & (1UL << 31));

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    target.corrupt_after_program = 1U;
    expect_status(
        "target readback mismatch",
        BOOT_FLASH_ERR_VERIFY,
        boot_flash_program_aligned(
            &flash,
            STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
            data,
            sizeof(data)
        )
    );

    make_target_host_flash(&target, &flash, only_slot_b, 1U);
    expect_status(
        "target wrong sector protected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_erase_sector(&flash, STM32F429_SLOT_A_FIRST_SECTOR)
    );
}

static void test_target_flash_backend_init_guards(void)
{
    boot_flash_target_host_context_t target;
    boot_flash_t flash;
    boot_flash_region_t canary = {
        STM32F429_SLOT_B_PAYLOAD_BASE,
        STM32F429_SLOT_B_PAYLOAD_BASE + 16UL,
    };

    boot_flash_target_host_context_init(&target);
    target.ramfunc_valid = 0U;
    expect_status(
        "target ramfunc invalid",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_target_init_host(&flash, &target, write_regions, 1U)
    );
    expect_status(
        "lab canary disabled",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_target_init_lab_canary(&flash, &canary, 1U)
    );
}

static void test_program_alignment_bounds_and_readback(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t data[16];
    uint8_t erased_bytes[16];
    uint8_t readback[16];

    for (size_t i = 0U; i < sizeof(data); ++i) {
        data[i] = (uint8_t)(0xA0U + i);
        erased_bytes[i] = 0xFFU;
    }

    make_flash(&sim, &flash);

    expect_status(
        "program unaligned address",
        BOOT_FLASH_ERR_ALIGNMENT,
        boot_flash_program_aligned(
            &flash,
            STM32F429_SLOT_B_SIGNED_IMAGE_BASE + 1UL,
            data,
            sizeof(data)
        )
    );
    expect_status(
        "program unaligned length",
        BOOT_FLASH_ERR_ALIGNMENT,
        boot_flash_program_aligned(&flash, STM32F429_SLOT_B_SIGNED_IMAGE_BASE, data, 9U)
    );
    expect_status(
        "program active slot rejected",
        BOOT_FLASH_ERR_PROTECTED_REGION,
        boot_flash_program_aligned(&flash, STM32F429_SLOT_A_SIGNED_IMAGE_BASE, data, sizeof(data))
    );
    expect_status(
        "program out of bounds",
        BOOT_FLASH_ERR_OUT_OF_BOUNDS,
        boot_flash_program_aligned(&flash, STM32F429_FLASH_END - 8UL, data, sizeof(data))
    );
    expect_status(
        "program address overflow",
        BOOT_FLASH_ERR_OUT_OF_BOUNDS,
        boot_flash_program_aligned(&flash, UINT32_MAX - 7UL, data, sizeof(data))
    );
    expect_u32("rejected programs did not reach backend", 0U, sim.program_count);

    expect_status(
        "program inactive slot",
        BOOT_FLASH_OK,
        boot_flash_program_aligned(&flash, STM32F429_SLOT_B_SIGNED_IMAGE_BASE, data, sizeof(data))
    );
    memset(readback, 0, sizeof(readback));
    expect_status(
        "read inactive slot",
        BOOT_FLASH_OK,
        boot_flash_read(&flash, STM32F429_SLOT_B_SIGNED_IMAGE_BASE, readback, sizeof(readback))
    );
    if (memcmp(data, readback, sizeof(data)) != 0) {
        printf("readback data mismatch\n");
        failures += 1;
    }

    expect_u32("first programmed address", STM32F429_SLOT_B_SIGNED_IMAGE_BASE, sim.last_program_address);
    expect_u32("first programmed length", (uint32_t)sizeof(data), (uint32_t)sim.last_program_length);

    expect_status(
        "program 0-to-1 rejected",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_program_aligned(
            &flash,
            STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
            erased_bytes,
            sizeof(erased_bytes)
        )
    );
}

static void test_read_rejects_address_overflow_before_backend(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t data[16];

    make_flash(&sim, &flash);
    expect_status(
        "read address overflow",
        BOOT_FLASH_ERR_OUT_OF_BOUNDS,
        boot_flash_read(&flash, UINT32_MAX - 7UL, data, sizeof(data))
    );
    expect_u32("overflow read did not reach backend", 0U, sim.read_count);
}

static void test_readback_verification_failure_is_reported(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t data[8] = {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U};

    make_flash(&sim, &flash);
    simulated_flash_corrupt_after_program(&sim, 1U);
    expect_status(
        "program verify detects corruption",
        BOOT_FLASH_ERR_VERIFY,
        boot_flash_program_aligned(&flash, STM32F429_BOOT_METADATA_A_BASE, data, sizeof(data))
    );
}

static void test_failure_injection_is_visible(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t data[8] = {0U};

    make_flash(&sim, &flash);
    simulated_flash_fail_after(&sim, 1U);
    expect_status(
        "erase failure injection",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_erase_sector(&flash, STM32F429_BOOT_METADATA_A_FIRST_SECTOR)
    );
    expect_u32("erase attempted once", 1U, sim.erase_count);

    make_flash(&sim, &flash);
    simulated_flash_fail_after(&sim, 1U);
    expect_status(
        "program failure injection",
        BOOT_FLASH_ERR_BACKEND,
        boot_flash_program_aligned(&flash, STM32F429_BOOT_METADATA_A_BASE, data, sizeof(data))
    );
    expect_u32("program attempted once", 1U, sim.program_count);
}

static void test_flash_init_rejects_invalid_policy(void)
{
    boot_flash_t flash;
    simulated_flash_t sim;
    const boot_flash_region_t bad_region[] = {
        {STM32F429_FLASH_END, STM32F429_FLASH_END + 8UL},
    };

    simulated_flash_init(&sim);
    expect_status(
        "bad alignment",
        BOOT_FLASH_ERR_INVALID_ARGUMENT,
        boot_flash_init(&flash, &sim, simulated_flash_ops(), write_regions, 1U, 7U)
    );
    expect_status(
        "bad write region",
        BOOT_FLASH_ERR_OUT_OF_BOUNDS,
        boot_flash_init(&flash, &sim, simulated_flash_ops(), bad_region, 1U, 8U)
    );
}

static boot_metadata_record_t metadata_empty_record(void)
{
    boot_metadata_record_t record;
    expect_metadata_status(
        "empty metadata",
        BOOT_METADATA_OK,
        boot_metadata_empty(&record)
    );
    return record;
}

static boot_metadata_record_t metadata_confirmed(
    const boot_metadata_record_t *current,
    uint32_t slot,
    uint32_t version
)
{
    boot_metadata_record_t next;
    expect_metadata_status(
        "prepare confirmed",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            current,
            BOOT_METADATA_STATE_CONFIRMED,
            slot,
            BOOT_SLOT_NONE,
            version,
            0U,
            1U,
            0U,
            &next
        )
    );
    return next;
}

static boot_metadata_record_t metadata_writing(
    const boot_metadata_record_t *current,
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t version
)
{
    boot_metadata_record_t next;
    expect_metadata_status(
        "prepare writing",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            current,
            BOOT_METADATA_STATE_WRITING,
            active_slot,
            candidate_slot,
            version,
            0U,
            0U,
            0U,
            &next
        )
    );
    return next;
}

static boot_metadata_record_t metadata_candidate_ready(
    const boot_metadata_record_t *current,
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t version
)
{
    boot_metadata_record_t next;
    expect_metadata_status(
        "prepare candidate ready",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            current,
            BOOT_METADATA_STATE_CANDIDATE_READY,
            active_slot,
            candidate_slot,
            version,
            0U,
            0U,
            0U,
            &next
        )
    );
    return next;
}

static boot_metadata_record_t metadata_pending(
    const boot_metadata_record_t *current,
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t version,
    uint32_t attempts
)
{
    boot_metadata_record_t next;
    expect_metadata_status(
        "prepare pending",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            current,
            BOOT_METADATA_STATE_PENDING_TRIAL,
            active_slot,
            candidate_slot,
            version,
            attempts,
            0U,
            0U,
            &next
        )
    );
    return next;
}

static void init_update_keys(void)
{
    uint8_t seed[32];

    for (uint32_t i = 0U; i < sizeof(seed); ++i) {
        seed[i] = (uint8_t)(0xA0U + i);
    }

    crypto_ed25519_key_pair(update_secret_key, update_public_key, seed);
}

static void sign_package_manifest(uint8_t *package)
{
    crypto_ed25519_sign(
        &package[SIGNED_MANIFEST_SIZE],
        update_secret_key,
        package,
        SIGNED_MANIFEST_SIZE
    );
}

static void refresh_package_hash_and_signature(uint8_t *package)
{
    signed_manifest_t manifest;
    uint8_t payload_hash[SIGNED_PAYLOAD_HASH_SIZE];

    expect_verify_status(
        "decode package for refresh",
        VERIFY_OK,
        signed_image_decode_manifest(package, &manifest)
    );
    crypto_sha512(
        payload_hash,
        &package[SIGNED_IMAGE_HEADER_SIZE],
        (size_t)manifest.image_size
    );
    memcpy(
        &package[offsetof(signed_manifest_t, payload_sha512)],
        payload_hash,
        sizeof(payload_hash)
    );
    sign_package_manifest(package);
}

static size_t build_update_package_for_slot_with_payload_size(
    uint32_t slot_id,
    uint32_t image_version,
    size_t payload_size,
    uint8_t *package,
    size_t package_capacity
)
{
    const boot_slot_descriptor_t *slot = NULL;
    uint8_t *payload = NULL;
    uint8_t payload_hash[SIGNED_PAYLOAD_HASH_SIZE];
    const size_t package_size =
        (size_t)SIGNED_IMAGE_HEADER_SIZE + payload_size;

    expect_u32(
        "package buffer capacity",
        1U,
        (package_capacity >= package_size) ? 1U : 0U
    );
    expect_u32(
        "lookup package slot",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(slot_id, &slot)
    );

    memset(package, 0xFF, package_size);
    payload = &package[SIGNED_IMAGE_HEADER_SIZE];
    memset(payload, 0xA5, payload_size);
    test_store_le32(&payload[0], APPLICATION_MSP_END);
    test_store_le32(&payload[4], slot->payload_base | 1UL);
    crypto_sha512(payload_hash, payload, payload_size);

    test_store_le32(&package[offsetof(signed_manifest_t, magic)], SIGNED_IMAGE_MAGIC);
    test_store_le32(
        &package[offsetof(signed_manifest_t, header_version)],
        UPDATE_PACKAGE_FORMAT_VERSION
    );
    test_store_le32(
        &package[offsetof(signed_manifest_t, image_version)],
        image_version
    );
    test_store_le32(
        &package[offsetof(signed_manifest_t, vector_address)],
        slot->payload_base
    );
    test_store_le32(
        &package[offsetof(signed_manifest_t, image_size)],
        (uint32_t)payload_size
    );
    test_store_le32(&package[offsetof(signed_manifest_t, flags)], 0U);
    test_store_le32(
        &package[offsetof(signed_manifest_t, reserved0)],
        UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1
    );
    test_store_le32(
        &package[offsetof(signed_manifest_t, reserved1)],
        UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION
    );
    memcpy(
        &package[offsetof(signed_manifest_t, payload_sha512)],
        payload_hash,
        sizeof(payload_hash)
    );
    sign_package_manifest(package);

    return package_size;
}

static size_t build_update_package_for_slot(
    uint32_t slot_id,
    uint32_t image_version,
    uint8_t *package,
    size_t package_capacity
)
{
    return build_update_package_for_slot_with_payload_size(
        slot_id,
        image_version,
        TEST_UPDATE_PAYLOAD_SIZE,
        package,
        package_capacity
    );
}

static void install_package_bytes_direct(
    simulated_flash_t *sim,
    const boot_slot_descriptor_t *slot,
    const uint8_t *package,
    size_t package_size
)
{
    memcpy(
        &sim->storage[flash_offset(slot->signed_image_base)],
        package,
        package_size
    );
    simulated_flash_sync_mapped(sim);
}

typedef struct {
    const boot_flash_t *flash;
    uint8_t *image_buffer;
    size_t image_buffer_size;
    const uint8_t *public_key;
} selection_verify_context_t;

static boot_slot_selection_status_t verify_slot_from_simulated_flash(
    void *context,
    const boot_slot_descriptor_t *slot,
    signed_image_jump_context_t *jump_context,
    verify_status_t *verify_status
)
{
    selection_verify_context_t *config =
        (selection_verify_context_t *)context;
    signed_manifest_t manifest;
    update_package_t package;
    update_package_status_t package_status;
    size_t package_size = 0U;

    if ((config == NULL) ||
        (config->flash == NULL) ||
        (config->image_buffer == NULL) ||
        (config->public_key == NULL) ||
        (slot == NULL) ||
        (jump_context == NULL) ||
        (verify_status == NULL) ||
        (config->image_buffer_size < (size_t)SIGNED_IMAGE_HEADER_SIZE)) {
        return BOOT_SLOT_SELECTION_ERR_INVALID_ARGUMENT;
    }

    *verify_status = VERIFY_BAD_PAYLOAD_RANGE;

    if (boot_flash_read(
            config->flash,
            slot->signed_image_base,
            config->image_buffer,
            (size_t)SIGNED_IMAGE_HEADER_SIZE
        ) != BOOT_FLASH_OK) {
        return BOOT_SLOT_SELECTION_ERR_VERIFY;
    }

    *verify_status = signed_image_decode_manifest(
        config->image_buffer,
        &manifest
    );
    if (*verify_status != VERIFY_OK) {
        return BOOT_SLOT_SELECTION_ERR_VERIFY;
    }

    package_size = (size_t)SIGNED_IMAGE_HEADER_SIZE + (size_t)manifest.image_size;
    if ((package_size < (size_t)SIGNED_IMAGE_HEADER_SIZE) ||
        (package_size > config->image_buffer_size)) {
        return BOOT_SLOT_SELECTION_ERR_VERIFY;
    }

    if (boot_flash_read(
            config->flash,
            slot->signed_image_base + SIGNED_IMAGE_HEADER_SIZE,
            &config->image_buffer[SIGNED_IMAGE_HEADER_SIZE],
            (size_t)manifest.image_size
        ) != BOOT_FLASH_OK) {
        return BOOT_SLOT_SELECTION_ERR_VERIFY;
    }

    package_status = update_package_verify_for_slot(
        config->image_buffer,
        package_size,
        config->public_key,
        slot,
        &package,
        verify_status
    );
    if (package_status != UPDATE_PACKAGE_OK) {
        return BOOT_SLOT_SELECTION_ERR_VERIFY;
    }

    *verify_status = signed_image_prepare_update_slot_buffer(
        package.manifest_bytes,
        package.payload,
        package.payload_size,
        slot,
        jump_context
    );

    return (*verify_status == VERIFY_OK)
        ? BOOT_SLOT_SELECTION_OK
        : BOOT_SLOT_SELECTION_ERR_VERIFY;
}

static void commit_confirmed_metadata(
    boot_flash_t *flash,
    uint32_t active_slot,
    uint32_t version
)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, active_slot, version);

    expect_metadata_status(
        "commit confirmed metadata",
        BOOT_METADATA_OK,
        boot_metadata_commit(flash, &confirmed)
    );
}

static update_install_options_t make_install_options_with_sizes(
    uint8_t *program_buffer,
    size_t program_buffer_size,
    uint8_t *readback_buffer,
    size_t readback_buffer_size,
    update_install_fault_hook_t hook,
    void *hook_context
)
{
    update_install_options_t options;

    options.program_buffer = program_buffer;
    options.program_buffer_size = program_buffer_size;
    options.readback_buffer = readback_buffer;
    options.readback_buffer_size = readback_buffer_size;
    options.fault_hook = hook;
    options.fault_context = hook_context;
    return options;
}

static update_install_options_t make_install_options(
    uint8_t *program_buffer,
    uint8_t *readback_buffer,
    update_install_fault_hook_t hook,
    void *hook_context
)
{
    return make_install_options_with_sizes(
        program_buffer,
        TEST_INSTALL_PROGRAM_CHUNK,
        readback_buffer,
        TEST_INSTALL_PROGRAM_CHUNK,
        hook,
        hook_context
    );
}

typedef struct {
    update_install_fault_point_t point;
    uint32_t detail;
    uint8_t match_detail;
} install_fault_config_t;

static update_install_status_t install_fault_hook(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
)
{
    const install_fault_config_t *config =
        (const install_fault_config_t *)context;

    if ((config != NULL) &&
        (config->point == point) &&
        ((config->match_detail == 0U) || (config->detail == detail))) {
        return UPDATE_INSTALL_ERR_INJECTED;
    }

    return UPDATE_INSTALL_OK;
}

static update_install_status_t stream_package_with_block_size(
    const boot_flash_t *flash,
    const uint8_t *package,
    size_t package_size,
    size_t block_size,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
)
{
    update_installer_session_t session;
    update_install_status_t install_status;
    size_t payload_offset = 0U;
    size_t payload_size = 0U;

    if ((package_size < (size_t)SIGNED_IMAGE_HEADER_SIZE) ||
        (block_size == 0U)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    payload_size = package_size - (size_t)SIGNED_IMAGE_HEADER_SIZE;

    install_status = update_installer_session_init(
        &session,
        flash,
        public_key,
        options,
        result
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return install_status;
    }

    install_status = update_installer_begin(
        &session,
        package,
        (size_t)SIGNED_IMAGE_HEADER_SIZE
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return install_status;
    }

    while (payload_offset < payload_size) {
        size_t chunk = payload_size - payload_offset;

        if (chunk > block_size) {
            chunk = block_size;
        }

        install_status = update_installer_write(
            &session,
            payload_offset,
            &package[SIGNED_IMAGE_HEADER_SIZE + payload_offset],
            chunk
        );
        if (install_status != UPDATE_INSTALL_OK) {
            return install_status;
        }

        payload_offset += chunk;
    }

    return update_installer_finish(&session);
}

typedef struct {
    boot_slot_selection_fault_point_t point;
    uint32_t detail;
    uint8_t match_detail;
} selection_fault_config_t;

static boot_slot_selection_status_t selection_fault_hook(
    void *context,
    boot_slot_selection_fault_point_t point,
    uint32_t detail
)
{
    const selection_fault_config_t *config =
        (const selection_fault_config_t *)context;

    if ((config != NULL) &&
        (config->point == point) &&
        ((config->match_detail == 0U) || (config->detail == detail))) {
        return BOOT_SLOT_SELECTION_ERR_INJECTED;
    }

    return BOOT_SLOT_SELECTION_OK;
}

static size_t setup_install_with_payload_size(
    simulated_flash_t *sim,
    boot_flash_t *flash,
    uint32_t active_slot_id,
    uint32_t active_version,
    uint32_t package_version,
    size_t payload_size,
    uint8_t *package,
    size_t package_capacity,
    const boot_slot_descriptor_t **active,
    const boot_slot_descriptor_t **candidate
)
{
    make_installer_flash(sim, flash);
    commit_confirmed_metadata(flash, active_slot_id, active_version);

    expect_u32(
        "lookup setup active slot",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(active_slot_id, active)
    );
    const uint32_t candidate_slot_id =
        (active_slot_id == (uint32_t)BOOT_SLOT_A)
            ? (uint32_t)BOOT_SLOT_B
            : (uint32_t)BOOT_SLOT_A;
    expect_u32(
        "lookup setup candidate slot",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(candidate_slot_id, candidate)
    );

    prepare_protected_snapshots(sim, *active);
    return build_update_package_for_slot_with_payload_size(
        candidate_slot_id,
        package_version,
        payload_size,
        package,
        package_capacity
    );
}

static size_t setup_standard_install(
    simulated_flash_t *sim,
    boot_flash_t *flash,
    uint32_t active_slot_id,
    uint32_t active_version,
    uint32_t package_version,
    uint8_t *package,
    size_t package_capacity,
    const boot_slot_descriptor_t **active,
    const boot_slot_descriptor_t **candidate
)
{
    return setup_install_with_payload_size(
        sim,
        flash,
        active_slot_id,
        active_version,
        package_version,
        TEST_UPDATE_PAYLOAD_SIZE,
        package,
        package_capacity,
        active,
        candidate
    );
}

static size_t setup_streaming_install_with_bootable_active(
    simulated_flash_t *sim,
    boot_flash_t *flash,
    uint32_t active_slot_id,
    uint32_t active_version,
    uint32_t package_version,
    size_t payload_size,
    uint8_t *package,
    size_t package_capacity,
    const boot_slot_descriptor_t **active,
    const boot_slot_descriptor_t **candidate
)
{
    uint8_t active_package[TEST_PACKAGE_BUFFER_SIZE];
    size_t active_package_size = 0U;
    const uint32_t candidate_slot_id =
        (active_slot_id == (uint32_t)BOOT_SLOT_A)
            ? (uint32_t)BOOT_SLOT_B
            : (uint32_t)BOOT_SLOT_A;

    make_installer_flash(sim, flash);
    expect_u32(
        "lookup streaming active slot",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(active_slot_id, active)
    );
    expect_u32(
        "lookup streaming candidate slot",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(candidate_slot_id, candidate)
    );

    fill_flash_region(
        sim,
        STM32F429_BOOTLOADER_BASE,
        STM32F429_BOOTLOADER_SIZE,
        0x10U
    );
    fill_flash_region(sim, (*active)->signed_image_base, STM32F429_SLOT_A_SIZE, 0x40U);
    fill_flash_region(sim, STM32F429_RECOVERY_BASE, STM32F429_RECOVERY_SIZE, 0x80U);

    active_package_size = build_update_package_for_slot(
        active_slot_id,
        active_version,
        active_package,
        sizeof(active_package)
    );
    install_package_bytes_direct(sim, *active, active_package, active_package_size);
    commit_confirmed_metadata(flash, active_slot_id, active_version);
    snapshot_flash_region(
        sim,
        STM32F429_BOOTLOADER_BASE,
        protected_stage0_before,
        sizeof(protected_stage0_before)
    );
    snapshot_flash_region(
        sim,
        (*active)->signed_image_base,
        protected_slot_before,
        sizeof(protected_slot_before)
    );
    snapshot_flash_region(
        sim,
        STM32F429_RECOVERY_BASE,
        protected_recovery_before,
        sizeof(protected_recovery_before)
    );

    return build_update_package_for_slot_with_payload_size(
        candidate_slot_id,
        package_version,
        payload_size,
        package,
        package_capacity
    );
}

static boot_metadata_record_t commit_candidate_ready_metadata(
    boot_flash_t *flash,
    uint32_t active_slot,
    uint32_t active_version,
    uint32_t candidate_slot,
    uint32_t candidate_version
)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, active_slot, active_version);
    boot_metadata_record_t writing =
        metadata_writing(&confirmed, active_slot, candidate_slot, candidate_version);
    boot_metadata_record_t ready =
        metadata_candidate_ready(&writing, active_slot, candidate_slot, candidate_version);

    expect_metadata_status(
        "commit selection confirmed",
        BOOT_METADATA_OK,
        boot_metadata_commit(flash, &confirmed)
    );
    expect_metadata_status(
        "commit selection writing",
        BOOT_METADATA_OK,
        boot_metadata_commit(flash, &writing)
    );
    expect_metadata_status(
        "commit selection ready",
        BOOT_METADATA_OK,
        boot_metadata_commit(flash, &ready)
    );
    return ready;
}

static boot_metadata_record_t commit_pending_metadata(
    boot_flash_t *flash,
    uint32_t active_slot,
    uint32_t active_version,
    uint32_t candidate_slot,
    uint32_t candidate_version,
    uint32_t attempts
)
{
    boot_metadata_record_t ready = commit_candidate_ready_metadata(
        flash,
        active_slot,
        active_version,
        candidate_slot,
        candidate_version
    );
    boot_metadata_record_t pending = metadata_pending(
        &ready,
        active_slot,
        candidate_slot,
        candidate_version,
        attempts
    );

    expect_metadata_status(
        "commit selection pending",
        BOOT_METADATA_OK,
        boot_metadata_commit(flash, &pending)
    );
    return pending;
}

static void setup_selection_images(
    simulated_flash_t *sim,
    uint32_t active_slot_id,
    uint32_t active_version,
    uint32_t candidate_version,
    const boot_slot_descriptor_t **active,
    const boot_slot_descriptor_t **candidate
)
{
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    size_t package_size = 0U;
    const uint32_t candidate_slot_id =
        (active_slot_id == (uint32_t)BOOT_SLOT_A)
            ? (uint32_t)BOOT_SLOT_B
            : (uint32_t)BOOT_SLOT_A;

    expect_u32(
        "lookup selection active",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(active_slot_id, active)
    );
    expect_u32(
        "lookup selection candidate",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(candidate_slot_id, candidate)
    );

    package_size = build_update_package_for_slot(
        active_slot_id,
        active_version,
        package,
        sizeof(package)
    );
    install_package_bytes_direct(sim, *active, package, package_size);

    package_size = build_update_package_for_slot(
        candidate_slot_id,
        candidate_version,
        package,
        sizeof(package)
    );
    install_package_bytes_direct(sim, *candidate, package, package_size);
}

static boot_slot_selection_options_t make_selection_options(
    const boot_flash_t *flash,
    selection_verify_context_t *verify_context,
    uint8_t *image_buffer,
    boot_slot_selection_fault_hook_t fault_hook,
    void *fault_context
)
{
    verify_context->flash = flash;
    verify_context->image_buffer = image_buffer;
    verify_context->image_buffer_size = TEST_PACKAGE_BUFFER_SIZE;
    verify_context->public_key = update_public_key;

    boot_slot_selection_options_t options = {
        .metadata_flash = flash,
        .verify_slot = verify_slot_from_simulated_flash,
        .verify_context = verify_context,
        .fault_hook = fault_hook,
        .fault_context = fault_context,
    };

    return options;
}

static void expect_reset_selects_active(
    const char *name,
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *active
)
{
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;
    boot_slot_selection_options_t options = make_selection_options(
        flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );

    expect_selection_status(
        name,
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32("reset selected active slot", active->id, selection.selected_slot);
    if (selection.decision == BOOT_SLOT_SELECTION_DECISION_TRIAL) {
        printf("%s: reset selected trial candidate unexpectedly\n", name);
        failures += 1;
    }
}

static void expect_no_forbidden_writes(
    const simulated_flash_t *sim,
    const boot_slot_descriptor_t *active
)
{
    expect_u32("write log did not overflow", 0U, sim->write_log_overflow);
    expect_u32(
        "no Stage 0 writes",
        0U,
        count_writes_in_range(
            sim,
            STM32F429_BOOTLOADER_BASE,
            STM32F429_BOOTLOADER_END
        )
    );
    expect_u32(
        "no active-slot writes",
        0U,
        count_writes_in_range(sim, active->signed_image_base, active->slot_end)
    );
    expect_u32(
        "no recovery writes",
        0U,
        count_writes_in_range(sim, STM32F429_RECOVERY_BASE, STM32F429_RECOVERY_END)
    );
    expect_u32(
        "no update-metadata raw writes",
        0U,
        count_writes_in_range(
            sim,
            STM32F429_UPDATE_METADATA_BASE,
            STM32F429_UPDATE_METADATA_END
        )
    );
}

static void expect_install_failure_invariants(
    const simulated_flash_t *sim,
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *active
)
{
    boot_metadata_record_t recovered;
    boot_metadata_status_t metadata_status =
        boot_metadata_recover_from_flash(flash, &recovered, NULL);

    expect_protected_regions_unchanged(sim, active);
    expect_no_forbidden_writes(sim, active);
    if (metadata_status == BOOT_METADATA_OK &&
        recovered.state == BOOT_METADATA_STATE_CANDIDATE_READY) {
        printf("failed install marked candidate ready\n");
        failures += 1;
    }
}

static void test_metadata_encode_decode_and_recovery(void)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t decoded;
    boot_metadata_record_t selected;
    boot_metadata_recovery_t recovery;
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];

    expect_metadata_status(
        "encode confirmed",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed, copy_a)
    );
    expect_metadata_status(
        "decode confirmed",
        BOOT_METADATA_OK,
        boot_metadata_decode(copy_a, sizeof(copy_a), &decoded)
    );
    expect_metadata_record(
        "decoded confirmed",
        &decoded,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );

    memcpy(copy_b, copy_a, sizeof(copy_b));
    copy_b[12] ^= 0x01U;
    expect_metadata_status(
        "corrupt crc rejected",
        BOOT_METADATA_ERR_CRC,
        boot_metadata_decode(copy_b, sizeof(copy_b), &decoded)
    );

    memset(copy_b, 0xFF, sizeof(copy_b));
    expect_metadata_status(
        "recover single copy",
        BOOT_METADATA_OK,
        boot_metadata_recover(copy_a, copy_b, sizeof(copy_a), &selected, &recovery)
    );
    expect_u32("copy A valid", 1U, recovery.copy_a_valid);
    expect_u32("copy B invalid", 0U, recovery.copy_b_valid);
    expect_u32("selected copy A", BOOT_METADATA_COPY_A, recovery.selected_copy);
    expect_metadata_record(
        "selected confirmed",
        &selected,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );

    boot_metadata_record_t writing =
        metadata_writing(&confirmed, BOOT_SLOT_A, BOOT_SLOT_B, 3U);
    expect_metadata_status(
        "encode writing",
        BOOT_METADATA_OK,
        boot_metadata_encode(&writing, copy_b)
    );
    expect_metadata_status(
        "recover highest sequence",
        BOOT_METADATA_OK,
        boot_metadata_recover(copy_a, copy_b, sizeof(copy_a), &selected, &recovery)
    );
    expect_u32("selected copy B", BOOT_METADATA_COPY_B, recovery.selected_copy);
    expect_metadata_record(
        "selected writing",
        &selected,
        2U,
        BOOT_METADATA_STATE_WRITING,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );

    memset(copy_a, 0xFF, sizeof(copy_a));
    memset(copy_b, 0xFF, sizeof(copy_b));
    expect_metadata_status(
        "both copies invalid",
        BOOT_METADATA_ERR_NO_VALID_COPY,
        boot_metadata_recover(copy_a, copy_b, sizeof(copy_a), &selected, &recovery)
    );
    expect_metadata_record(
        "selected empty after invalid",
        &selected,
        0U,
        BOOT_METADATA_STATE_EMPTY,
        BOOT_SLOT_NONE,
        BOOT_SLOT_NONE,
        0U
    );
}

static void test_metadata_rejects_noncanonical_records(void)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t decoded;
    uint8_t encoded[STM32F429_BOOT_METADATA_RECORD_SIZE];

    expect_metadata_status(
        "encode confirmed for corruption tests",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed, encoded)
    );

    test_store_le32(&encoded[16], 99UL);
    refresh_metadata_crc(encoded);
    expect_metadata_status(
        "decode unknown state rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_decode(encoded, sizeof(encoded), &decoded)
    );

    expect_metadata_status(
        "re-encode confirmed for slot corruption",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed, encoded)
    );
    test_store_le32(&encoded[20], 99UL);
    refresh_metadata_crc(encoded);
    expect_metadata_status(
        "decode invalid active slot rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_decode(encoded, sizeof(encoded), &decoded)
    );

    expect_metadata_status(
        "re-encode confirmed for reserved corruption",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed, encoded)
    );
    test_store_le32(&encoded[44], 1UL);
    refresh_metadata_crc(encoded);
    expect_metadata_status(
        "decode reserved field rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_decode(encoded, sizeof(encoded), &decoded)
    );

    expect_metadata_status(
        "re-encode confirmed for padding corruption",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed, encoded)
    );
    encoded[TEST_METADATA_PADDING_OFFSET] = 1U;
    expect_metadata_status(
        "decode padding rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_decode(encoded, sizeof(encoded), &decoded)
    );

    expect_metadata_status(
        "re-encode confirmed for commit corruption",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed, encoded)
    );
    encoded[TEST_METADATA_COMMIT_OFFSET] ^= 1U;
    expect_metadata_status(
        "decode missing commit marker rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_decode(encoded, sizeof(encoded), &decoded)
    );

    expect_metadata_status(
        "decode truncated metadata rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_decode(
            encoded,
            STM32F429_BOOT_METADATA_RECORD_SIZE - 1U,
            &decoded
        )
    );

    confirmed.state = (boot_metadata_state_t)99;
    expect_metadata_status(
        "encode unknown state rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_encode(&confirmed, encoded)
    );
}

static void test_metadata_identical_sequence_is_deterministic(void)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t selected;
    boot_metadata_recovery_t recovery;
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];

    expect_metadata_status(
        "encode identical copy A",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed, copy_a)
    );
    memcpy(copy_b, copy_a, sizeof(copy_b));
    expect_metadata_status(
        "recover identical equal sequence",
        BOOT_METADATA_OK,
        boot_metadata_recover(copy_a, copy_b, sizeof(copy_a), &selected, &recovery)
    );
    expect_u32("identical copy A valid", 1U, recovery.copy_a_valid);
    expect_u32("identical copy B valid", 1U, recovery.copy_b_valid);
    expect_u32("identical selected copy A", BOOT_METADATA_COPY_A, recovery.selected_copy);
    expect_metadata_record(
        "identical selected confirmed",
        &selected,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );
}

static void test_metadata_ambiguous_and_sequence_handling(void)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed_a =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t confirmed_b = confirmed_a;
    boot_metadata_record_t selected;
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];

    confirmed_b.active_slot = BOOT_SLOT_B;
    expect_metadata_status(
        "encode confirmed A",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed_a, copy_a)
    );
    expect_metadata_status(
        "encode confirmed B same sequence",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed_b, copy_b)
    );
    expect_metadata_status(
        "same sequence conflict ambiguous",
        BOOT_METADATA_ERR_AMBIGUOUS,
        boot_metadata_recover(copy_a, copy_b, sizeof(copy_a), &selected, NULL)
    );

    empty.sequence = UINT32_MAX - 1UL;
    expect_metadata_status(
        "sequence exhausted",
        BOOT_METADATA_ERR_SEQUENCE,
        boot_metadata_prepare_next(
            &empty,
            BOOT_METADATA_STATE_WRITING,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U,
            0U,
            0U,
            0U,
            &selected
        )
    );
}

static void test_metadata_state_transitions(void)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t writing =
        metadata_writing(&confirmed, BOOT_SLOT_A, BOOT_SLOT_B, 3U);
    boot_metadata_record_t ready;
    boot_metadata_record_t pending;
    boot_metadata_record_t rejected;
    boot_metadata_record_t bad;

    expect_metadata_status(
        "writing to ready",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            &writing,
            BOOT_METADATA_STATE_CANDIDATE_READY,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U,
            0U,
            0U,
            0U,
            &ready
        )
    );
    expect_metadata_status(
        "ready to pending",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            &ready,
            BOOT_METADATA_STATE_PENDING_TRIAL,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U,
            1U,
            0U,
            0U,
            &pending
        )
    );
    expect_metadata_status(
        "pending to rejected",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            &pending,
            BOOT_METADATA_STATE_REJECTED_INVALID,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U,
            0U,
            0U,
            0xBADU,
            &rejected
        )
    );
    expect_metadata_status(
        "confirmed cannot skip to ready",
        BOOT_METADATA_ERR_BAD_TRANSITION,
        boot_metadata_prepare_next(
            &confirmed,
            BOOT_METADATA_STATE_CANDIDATE_READY,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U,
            0U,
            0U,
            0U,
            &bad
        )
    );
    expect_metadata_status(
        "pending can record exhausted attempts",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            &pending,
            BOOT_METADATA_STATE_PENDING_TRIAL,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U,
            0U,
            0U,
            0U,
            &bad
        )
    );
    expect_metadata_status(
        "pending decrement cannot skip attempts",
        BOOT_METADATA_ERR_BAD_TRANSITION,
        boot_metadata_prepare_next(
            &pending,
            BOOT_METADATA_STATE_PENDING_TRIAL,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U,
            3U,
            0U,
            0U,
            &bad
        )
    );
    expect_metadata_status(
        "same active and candidate rejected",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_prepare_next(
            &confirmed,
            BOOT_METADATA_STATE_WRITING,
            BOOT_SLOT_A,
            BOOT_SLOT_A,
            3U,
            0U,
            0U,
            0U,
            &bad
        )
    );
}

static void test_metadata_flash_commit_and_recovery(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t writing =
        metadata_writing(&confirmed, BOOT_SLOT_A, BOOT_SLOT_B, 3U);
    boot_metadata_record_t recovered;
    boot_metadata_recovery_t recovery;

    make_flash(&sim, &flash);
    expect_metadata_status(
        "commit initial confirmed",
        BOOT_METADATA_OK,
        boot_metadata_commit(&flash, &confirmed)
    );
    expect_metadata_status(
        "recover initial confirmed",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, &recovery)
    );
    expect_u32("initial selected copy A", BOOT_METADATA_COPY_A, recovery.selected_copy);
    expect_metadata_record(
        "initial recovered",
        &recovered,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );

    expect_metadata_status(
        "commit writing to alternate copy",
        BOOT_METADATA_OK,
        boot_metadata_commit(&flash, &writing)
    );
    expect_metadata_status(
        "recover writing",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, &recovery)
    );
    expect_u32("writing selected copy B", BOOT_METADATA_COPY_B, recovery.selected_copy);
    expect_metadata_record(
        "writing recovered",
        &recovered,
        2U,
        BOOT_METADATA_STATE_WRITING,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );
}

static void test_metadata_power_loss_during_copy_write_keeps_previous_copy(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t writing =
        metadata_writing(&confirmed, BOOT_SLOT_A, BOOT_SLOT_B, 3U);
    boot_metadata_record_t recovered;

    make_flash(&sim, &flash);
    expect_metadata_status(
        "commit initial before failure",
        BOOT_METADATA_OK,
        boot_metadata_commit(&flash, &confirmed)
    );

    simulated_flash_fail_after(&sim, 3U);
    expect_metadata_status(
        "fail during metadata copy 2 erase",
        BOOT_METADATA_ERR_FLASH,
        boot_metadata_commit(&flash, &writing)
    );
    expect_metadata_status(
        "recover confirmed after erase failure",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "confirmed after erase failure",
        &recovered,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );

    make_flash(&sim, &flash);
    expect_metadata_status(
        "commit initial before body failure",
        BOOT_METADATA_OK,
        boot_metadata_commit(&flash, &confirmed)
    );
    simulated_flash_fail_after(&sim, 4U);
    expect_metadata_status(
        "fail during metadata copy 2 body program",
        BOOT_METADATA_ERR_FLASH,
        boot_metadata_commit(&flash, &writing)
    );
    expect_metadata_status(
        "recover confirmed after body failure",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "confirmed after body failure",
        &recovered,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );
}

static void test_metadata_uncommitted_record_is_not_selected(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t writing =
        metadata_writing(&confirmed, BOOT_SLOT_A, BOOT_SLOT_B, 3U);
    boot_metadata_record_t recovered;
    boot_metadata_recovery_t recovery;
    uint8_t encoded[STM32F429_BOOT_METADATA_RECORD_SIZE];

    make_flash(&sim, &flash);
    expect_metadata_status(
        "commit confirmed before uncommitted write",
        BOOT_METADATA_OK,
        boot_metadata_commit(&flash, &confirmed)
    );
    expect_metadata_status(
        "encode uncommitted writing",
        BOOT_METADATA_OK,
        boot_metadata_encode(&writing, encoded)
    );
    expect_status(
        "erase candidate metadata copy",
        BOOT_FLASH_OK,
        boot_flash_erase_sector(&flash, STM32F429_BOOT_METADATA_B_FIRST_SECTOR)
    );
    expect_status(
        "program body without commit marker",
        BOOT_FLASH_OK,
        boot_flash_program_aligned(
            &flash,
            STM32F429_BOOT_METADATA_B_BASE,
            encoded,
            TEST_METADATA_COMMIT_OFFSET
        )
    );
    expect_metadata_status(
        "recover ignores uncommitted copy",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, &recovery)
    );
    expect_u32("uncommitted copy A valid", 1U, recovery.copy_a_valid);
    expect_u32("uncommitted copy B invalid", 0U, recovery.copy_b_valid);
    expect_u32("uncommitted selected copy A", BOOT_METADATA_COPY_A, recovery.selected_copy);
    expect_metadata_record(
        "uncommitted recovered confirmed",
        &recovered,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );
}

static void test_metadata_commit_failure_boundaries_are_deterministic(void)
{
    for (uint32_t operation = 1U; operation <= 10U; ++operation) {
        static simulated_flash_t sim;
        boot_flash_t flash;
        boot_metadata_record_t empty = metadata_empty_record();
        boot_metadata_record_t confirmed =
            metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
        boot_metadata_record_t writing =
            metadata_writing(&confirmed, BOOT_SLOT_A, BOOT_SLOT_B, 3U);
        boot_metadata_record_t recovered;

        make_flash(&sim, &flash);
        expect_metadata_status(
            "commit confirmed before injected failure",
            BOOT_METADATA_OK,
            boot_metadata_commit(&flash, &confirmed)
        );

        simulated_flash_fail_after(&sim, operation);
        expect_metadata_status(
            "commit with injected failure",
            BOOT_METADATA_ERR_FLASH,
            boot_metadata_commit(&flash, &writing)
        );
        expect_metadata_status(
            "recover after injected failure",
            BOOT_METADATA_OK,
            boot_metadata_recover_from_flash(&flash, &recovered, NULL)
        );

        if (operation <= 8U) {
            expect_metadata_record(
                "pre-commit-marker failure preserves confirmed copy",
                &recovered,
                1U,
                BOOT_METADATA_STATE_CONFIRMED,
                BOOT_SLOT_A,
                BOOT_SLOT_NONE,
                2U
            );
        } else {
            expect_metadata_record(
                "post-commit-marker failure recovers committed copy",
                &recovered,
                2U,
                BOOT_METADATA_STATE_WRITING,
                BOOT_SLOT_A,
                BOOT_SLOT_B,
                3U
            );
        }
    }
}

static void test_blank_device_can_be_factory_provisioned_and_boots_slot_a(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];
    boot_metadata_record_t decoded;
    boot_metadata_record_t recovered;
    boot_metadata_recovery_t recovery;
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );

    boot_slot_selection_options_t options = make_selection_options(
        &flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );
    expect_selection_status(
        "blank metadata fails closed",
        BOOT_SLOT_SELECTION_ERR_METADATA,
        boot_slot_selection_select(&options, &selection)
    );

    expect_metadata_status(
        "factory provision copy A",
        BOOT_METADATA_OK,
        boot_metadata_provision_confirmed_image(BOOT_SLOT_A, 2U, copy_a)
    );
    expect_metadata_status(
        "factory provision copy B",
        BOOT_METADATA_OK,
        boot_metadata_provision_confirmed_image(BOOT_SLOT_A, 2U, copy_b)
    );
    expect_u32(
        "factory copies deterministic",
        0U,
        (uint32_t)memcmp(copy_a, copy_b, sizeof(copy_a))
    );
    expect_metadata_status(
        "factory metadata decodes",
        BOOT_METADATA_OK,
        boot_metadata_decode(copy_a, sizeof(copy_a), &decoded)
    );
    expect_metadata_record(
        "factory metadata confirmed",
        &decoded,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );

    memcpy(
        &sim.storage[flash_offset(STM32F429_BOOT_METADATA_A_BASE)],
        copy_a,
        sizeof(copy_a)
    );
    memcpy(
        &sim.storage[flash_offset(STM32F429_BOOT_METADATA_B_BASE)],
        copy_b,
        sizeof(copy_b)
    );
    expect_metadata_status(
        "factory metadata recovers",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, &recovery)
    );
    expect_u32("factory copy A valid", 1U, recovery.copy_a_valid);
    expect_u32("factory copy B valid", 1U, recovery.copy_b_valid);
    expect_u32("factory selected copy A", BOOT_METADATA_COPY_A, recovery.selected_copy);
    expect_metadata_record(
        "factory recovered confirmed",
        &recovered,
        1U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_A,
        BOOT_SLOT_NONE,
        2U
    );

    expect_selection_status(
        "factory metadata boots Slot A",
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32(
        "factory boot decision confirmed",
        BOOT_SLOT_SELECTION_DECISION_CONFIRMED,
        selection.decision
    );
    expect_u32("factory selected Slot A", BOOT_SLOT_A, selection.selected_slot);
    expect_verify_status(
        "factory selected image verified",
        VERIFY_OK,
        selection.selected_verify_status
    );
    expect_u32(
        "factory reset vector in Slot A",
        active->payload_base | 1UL,
        selection.jump_context.reset_vector | 1UL
    );
    (void)candidate;
}

static void test_update_package_parse_and_verify_security_cases(void)
{
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t mutated[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t wrong_public_key[FIRMWARE_PUBLIC_KEY_SIZE];
    uint8_t wrong_secret_key[64];
    uint8_t wrong_seed[32];
    const boot_slot_descriptor_t *slot_b = NULL;
    const boot_slot_descriptor_t *slot_a = NULL;
    update_package_t parsed;
    verify_status_t verify_status = VERIFY_BAD_PAYLOAD_RANGE;

    for (uint32_t i = 0U; i < sizeof(wrong_seed); ++i) {
        wrong_seed[i] = (uint8_t)(0x55U + i);
    }
    crypto_ed25519_key_pair(wrong_secret_key, wrong_public_key, wrong_seed);

    expect_u32(
        "lookup slot B for package tests",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_B, &slot_b)
    );
    expect_u32(
        "lookup slot A for package tests",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_A, &slot_a)
    );

    const size_t package_size =
        build_update_package_for_slot(BOOT_SLOT_B, 3U, package, sizeof(package));

    expect_package_status(
        "valid update package",
        UPDATE_PACKAGE_OK,
        update_package_verify_for_slot(
            package,
            package_size,
            update_public_key,
            slot_b,
            &parsed,
            &verify_status
        )
    );
    expect_verify_status("valid update package verifier", VERIFY_OK, verify_status);
    expect_u32("parsed package payload", TEST_UPDATE_PAYLOAD_SIZE, (uint32_t)parsed.payload_size);

    expect_package_status(
        "wrong key rejected",
        UPDATE_PACKAGE_ERR_VERIFY,
        update_package_verify_for_slot(
            package,
            package_size,
            wrong_public_key,
            slot_b,
            NULL,
            &verify_status
        )
    );
    expect_verify_status("wrong key verify status", VERIFY_BAD_SIGNATURE, verify_status);

    memcpy(mutated, package, package_size);
    mutated[package_size - 1U] ^= 0x01U;
    expect_package_status(
        "modified payload rejected",
        UPDATE_PACKAGE_ERR_VERIFY,
        update_package_verify_for_slot(
            mutated,
            package_size,
            update_public_key,
            slot_b,
            NULL,
            &verify_status
        )
    );
    expect_verify_status(
        "modified payload verify status",
        VERIFY_BAD_PAYLOAD_HASH,
        verify_status
    );

    memcpy(mutated, package, package_size);
    test_store_le32(&mutated[offsetof(signed_manifest_t, image_version)], 4U);
    expect_package_status(
        "modified authenticated manifest rejected",
        UPDATE_PACKAGE_ERR_VERIFY,
        update_package_verify_for_slot(
            mutated,
            package_size,
            update_public_key,
            slot_b,
            NULL,
            &verify_status
        )
    );
    expect_verify_status(
        "modified manifest verify status",
        VERIFY_BAD_SIGNATURE,
        verify_status
    );

    memcpy(mutated, package, package_size);
    test_store_le32(&mutated[offsetof(signed_manifest_t, flags)], 1U);
    sign_package_manifest(mutated);
    expect_package_status(
        "unknown flags rejected",
        UPDATE_PACKAGE_ERR_UNKNOWN_FLAGS,
        update_package_parse(mutated, package_size, &parsed)
    );

    memcpy(mutated, package, package_size);
    test_store_le32(
        &mutated[offsetof(signed_manifest_t, header_version)],
        UPDATE_PACKAGE_FORMAT_VERSION + 1UL
    );
    sign_package_manifest(mutated);
    expect_package_status(
        "unsupported package version rejected",
        UPDATE_PACKAGE_ERR_UNSUPPORTED_VERSION,
        update_package_parse(mutated, package_size, &parsed)
    );

    expect_package_status(
        "truncated package rejected",
        UPDATE_PACKAGE_ERR_TRUNCATED,
        update_package_parse(package, package_size - 1U, &parsed)
    );

    memcpy(mutated, package, package_size);
    mutated[package_size] = 0xFFU;
    expect_package_status(
        "trailing package data rejected",
        UPDATE_PACKAGE_ERR_TRAILING_DATA,
        update_package_parse(mutated, package_size + 1U, &parsed)
    );

    memcpy(mutated, package, package_size);
    test_store_le32(
        &mutated[offsetof(signed_manifest_t, image_size)],
        STM32F429_SLOT_B_PAYLOAD_MAX_SIZE + 1UL
    );
    sign_package_manifest(mutated);
    expect_package_status(
        "oversized package rejected",
        UPDATE_PACKAGE_ERR_OVERSIZED,
        update_package_parse(mutated, package_size, &parsed)
    );

    memcpy(mutated, package, package_size);
    test_store_le32(&mutated[offsetof(signed_manifest_t, image_size)], 0U);
    sign_package_manifest(mutated);
    expect_package_status(
        "zero payload rejected",
        UPDATE_PACKAGE_ERR_BAD_SIZE,
        update_package_parse(mutated, package_size, &parsed)
    );

    memcpy(mutated, package, package_size);
    test_store_le32(&mutated[offsetof(signed_manifest_t, image_size)], UINT32_MAX);
    sign_package_manifest(mutated);
    expect_package_status(
        "address arithmetic overflow sized package rejected",
        UPDATE_PACKAGE_ERR_OVERSIZED,
        update_package_parse(mutated, package_size, &parsed)
    );

    memcpy(mutated, package, package_size);
    test_store_le32(
        &mutated[offsetof(signed_manifest_t, reserved0)],
        UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1 + 1UL
    );
    sign_package_manifest(mutated);
    expect_package_status(
        "bad target compatibility rejected",
        UPDATE_PACKAGE_ERR_BAD_TARGET,
        update_package_parse(mutated, package_size, &parsed)
    );

    memcpy(mutated, package, package_size);
    test_store_le32(
        &mutated[offsetof(signed_manifest_t, reserved1)],
        UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION + 1UL
    );
    sign_package_manifest(mutated);
    expect_package_status(
        "bad image type rejected",
        UPDATE_PACKAGE_ERR_BAD_IMAGE_TYPE,
        update_package_parse(mutated, package_size, &parsed)
    );

    memcpy(mutated, package, package_size);
    mutated[SIGNED_MANIFEST_SIZE + SIGNED_SIGNATURE_SIZE] = 0x00U;
    expect_package_status(
        "bad padding rejected",
        UPDATE_PACKAGE_ERR_BAD_PADDING,
        update_package_parse(mutated, package_size, &parsed)
    );

    expect_package_status(
        "package for inactive slot only",
        UPDATE_PACKAGE_ERR_INCOMPATIBLE_SLOT,
        update_package_verify_for_slot(
            package,
            package_size,
            update_public_key,
            slot_a,
            NULL,
            &verify_status
        )
    );

    memcpy(mutated, package, package_size);
    test_store_le32(&mutated[SIGNED_IMAGE_HEADER_SIZE], APPLICATION_MSP_BASE);
    refresh_package_hash_and_signature(mutated);
    expect_package_status(
        "invalid MSP rejected",
        UPDATE_PACKAGE_ERR_VERIFY,
        update_package_verify_for_slot(
            mutated,
            package_size,
            update_public_key,
            slot_b,
            NULL,
            &verify_status
        )
    );
    expect_verify_status("invalid MSP verify status", VERIFY_BAD_STACK, verify_status);

    memcpy(mutated, package, package_size);
    test_store_le32(
        &mutated[SIGNED_IMAGE_HEADER_SIZE + 4U],
        slot_b->signed_image_base | 1UL
    );
    refresh_package_hash_and_signature(mutated);
    expect_package_status(
        "reset before payload rejected",
        UPDATE_PACKAGE_ERR_VERIFY,
        update_package_verify_for_slot(
            mutated,
            package_size,
            update_public_key,
            slot_b,
            NULL,
            &verify_status
        )
    );
    expect_verify_status(
        "reset before payload verify status",
        VERIFY_BAD_RESET_VECTOR,
        verify_status
    );

    memcpy(mutated, package, package_size);
    test_store_le32(
        &mutated[SIGNED_IMAGE_HEADER_SIZE + 4U],
        (slot_b->payload_base + TEST_UPDATE_PAYLOAD_SIZE) | 1UL
    );
    refresh_package_hash_and_signature(mutated);
    expect_package_status(
        "reset outside payload rejected",
        UPDATE_PACKAGE_ERR_VERIFY,
        update_package_verify_for_slot(
            mutated,
            package_size,
            update_public_key,
            slot_b,
            NULL,
            &verify_status
        )
    );
    expect_verify_status(
        "reset outside payload verify status",
        VERIFY_BAD_RESET_VECTOR,
        verify_status
    );
}

static void test_update_installer_success_to_candidate_ready(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    update_install_result_t result;
    boot_metadata_record_t recovered;
    update_package_t installed;
    verify_status_t verify_status = VERIFY_BAD_PAYLOAD_RANGE;

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );

    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        NULL,
        NULL
    );

    expect_install_status(
        "install valid package",
        UPDATE_INSTALL_OK,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            &result
        )
    );
    expect_u32("install active slot", BOOT_SLOT_A, result.active_slot);
    expect_u32("install candidate slot", BOOT_SLOT_B, result.candidate_slot);
    expect_u32("install image version", 3U, result.image_version);
    expect_u32(
        "all inactive sectors erased",
        candidate->last_sector - candidate->first_sector + 1UL,
        result.erased_sector_count
    );
    expect_u32("program blocks", 6U, result.programmed_block_count);
    expect_verify_status("package preverify", VERIFY_OK, result.package_verify_status);
    expect_verify_status("installed verify", VERIFY_OK, result.installed_verify_status);

    expect_metadata_status(
        "recover candidate ready",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "candidate ready metadata",
        &recovered,
        3U,
        BOOT_METADATA_STATE_CANDIDATE_READY,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );

    expect_package_status(
        "installed bytes verify",
        UPDATE_PACKAGE_OK,
        update_package_verify_for_slot(
            &sim.storage[flash_offset(candidate->signed_image_base)],
            package_size,
            update_public_key,
            candidate,
            &installed,
            &verify_status
        )
    );
    expect_verify_status("installed bytes verifier", VERIFY_OK, verify_status);
    expect_protected_regions_unchanged(&sim, active);
    expect_no_forbidden_writes(&sim, active);
}

static void test_update_installer_installs_package_larger_than_io_buffers(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_LARGE_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
    uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
    update_install_result_t result;

    const size_t package_size = setup_install_with_payload_size(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        TEST_LARGE_UPDATE_PAYLOAD_SIZE,
        package,
        sizeof(package),
        &active,
        &candidate
    );

    update_install_options_t options = make_install_options_with_sizes(
        program_buffer,
        sizeof(program_buffer),
        readback_buffer,
        sizeof(readback_buffer),
        NULL,
        NULL
    );

    expect_u32(
        "large package exceeds program buffer",
        1U,
        (package_size > sizeof(program_buffer)) ? 1U : 0U
    );
    expect_u32(
        "large package exceeds readback buffer",
        1U,
        (package_size > sizeof(readback_buffer)) ? 1U : 0U
    );
    expect_install_status(
        "install large package with small buffers",
        UPDATE_INSTALL_OK,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            &result
        )
    );
    expect_u32("large install active slot", BOOT_SLOT_A, result.active_slot);
    expect_u32("large install candidate slot", BOOT_SLOT_B, result.candidate_slot);
    expect_u32("large install image version", 3U, result.image_version);
    expect_u32("large install program blocks", 5U, result.programmed_block_count);
    expect_verify_status("large package preverify", VERIFY_OK, result.package_verify_status);
    expect_verify_status("large installed verify", VERIFY_OK, result.installed_verify_status);
    expect_protected_regions_unchanged(&sim, active);
    expect_no_forbidden_writes(&sim, active);
    (void)candidate;
}

static void test_update_installer_uses_opposite_inactive_slot(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    update_install_result_t result;

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_B,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        NULL,
        NULL
    );

    expect_install_status(
        "install to slot A when B active",
        UPDATE_INSTALL_OK,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            &result
        )
    );
    expect_u32("slot B remained active", BOOT_SLOT_B, result.active_slot);
    expect_u32("slot A selected as candidate", BOOT_SLOT_A, result.candidate_slot);
    expect_protected_regions_unchanged(&sim, active);
    expect_no_forbidden_writes(&sim, active);
}

static void test_update_installer_rejects_rollback_and_active_slot_package(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

    size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        3U,
        2U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        NULL,
        NULL
    );
    expect_install_status(
        "rollback package rejected",
        UPDATE_INSTALL_ERR_ROLLBACK,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);

    package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    (void)candidate;
    package_size = build_update_package_for_slot(
        BOOT_SLOT_A,
        3U,
        package,
        sizeof(package)
    );
    prepare_protected_snapshots(&sim, active);
    expect_install_status(
        "active-slot package rejected",
        UPDATE_INSTALL_ERR_PACKAGE,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
}

static void test_update_installer_rejects_bad_metadata_states(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    boot_metadata_record_t recovered;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];

    make_installer_flash(&sim, &flash);
    expect_u32(
        "lookup active for invalid metadata",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_A, &active)
    );
    prepare_protected_snapshots(&sim, active);
    const size_t package_size =
        build_update_package_for_slot(BOOT_SLOT_B, 3U, package, sizeof(package));
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        NULL,
        NULL
    );

    expect_install_status(
        "both metadata copies invalid",
        UPDATE_INSTALL_ERR_METADATA,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_no_forbidden_writes(&sim, active);

    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed_a =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t confirmed_b = confirmed_a;
    confirmed_b.active_slot = BOOT_SLOT_B;
    expect_metadata_status(
        "encode ambiguous A",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed_a, copy_a)
    );
    expect_metadata_status(
        "encode ambiguous B",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed_b, copy_b)
    );
    memcpy(&sim.storage[flash_offset(STM32F429_BOOT_METADATA_A_BASE)], copy_a, sizeof(copy_a));
    memcpy(&sim.storage[flash_offset(STM32F429_BOOT_METADATA_B_BASE)], copy_b, sizeof(copy_b));
    sim.write_log_count = 0U;
    expect_install_status(
        "ambiguous active-slot state rejected",
        UPDATE_INSTALL_ERR_METADATA,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_metadata_status(
        "ambiguous metadata remains ambiguous",
        BOOT_METADATA_ERR_AMBIGUOUS,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_no_forbidden_writes(&sim, active);
}

static void run_install_with_hook_failure(
    const char *name,
    update_install_fault_point_t point,
    uint32_t detail,
    uint8_t match_detail
)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    install_fault_config_t fault = {point, detail, match_detail};

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    (void)candidate;
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        install_fault_hook,
        &fault
    );

    expect_install_status(
        name,
        UPDATE_INSTALL_ERR_INJECTED,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
}

static void test_update_installer_logical_failure_injection(void)
{
    run_install_with_hook_failure(
        "metadata WRITING transition injection",
        UPDATE_INSTALL_FAULT_METADATA_WRITING,
        0U,
        0U
    );
    run_install_with_hook_failure(
        "first program block injection",
        UPDATE_INSTALL_FAULT_PROGRAM_BLOCK,
        0U,
        1U
    );
    run_install_with_hook_failure(
        "middle program block injection",
        UPDATE_INSTALL_FAULT_PROGRAM_BLOCK,
        2U,
        1U
    );
    run_install_with_hook_failure(
        "final program block injection",
        UPDATE_INSTALL_FAULT_PROGRAM_BLOCK,
        5U,
        1U
    );
    run_install_with_hook_failure(
        "installer readback injection",
        UPDATE_INSTALL_FAULT_READBACK,
        0U,
        1U
    );
    run_install_with_hook_failure(
        "complete hash injection",
        UPDATE_INSTALL_FAULT_HASH_COMPLETE,
        3U,
        1U
    );
    run_install_with_hook_failure(
        "production verifier invocation injection",
        UPDATE_INSTALL_FAULT_VERIFY_INSTALLED,
        3U,
        1U
    );
    run_install_with_hook_failure(
        "candidate-ready metadata transition injection",
        UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY,
        3U,
        1U
    );
}

static void test_update_installer_flash_failure_injection(void)
{
    for (uint32_t sector = STM32F429_SLOT_B_FIRST_SECTOR;
         sector <= STM32F429_SLOT_B_LAST_SECTOR;
         ++sector) {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
        uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        (void)candidate;
        simulated_flash_fail_before_erase_sector(&sim, sector);
        update_install_options_t options = make_install_options(
            program_buffer,
            readback_buffer,
            NULL,
            NULL
        );
        expect_install_status(
            "inactive sector erase failure",
            UPDATE_INSTALL_ERR_ERASE,
            update_installer_install(
                &flash,
                package,
                package_size,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_failure_invariants(&sim, &flash, active);
    }

    for (uint32_t block = 0U; block < 6U; block += 5U) {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
        uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        simulated_flash_fail_before_program_address(
            &sim,
            candidate->signed_image_base + (block * TEST_INSTALL_PROGRAM_CHUNK)
        );
        update_install_options_t options = make_install_options(
            program_buffer,
            readback_buffer,
            NULL,
            NULL
        );
        expect_install_status(
            "program failure",
            UPDATE_INSTALL_ERR_PROGRAM,
            update_installer_install(
                &flash,
                package,
                package_size,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_failure_invariants(&sim, &flash, active);
    }

    {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
        uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        simulated_flash_fail_before_read_address(&sim, candidate->signed_image_base);
        update_install_options_t options = make_install_options(
            program_buffer,
            readback_buffer,
            NULL,
            NULL
        );
        expect_install_status(
            "flash read-back failure",
            UPDATE_INSTALL_ERR_PROGRAM,
            update_installer_install(
                &flash,
                package,
                package_size,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_failure_invariants(&sim, &flash, active);
    }

    {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
        uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        simulated_flash_fail_before_program_address(
            &sim,
            STM32F429_BOOT_METADATA_A_BASE + TEST_METADATA_COMMIT_OFFSET
        );
        update_install_options_t options = make_install_options(
            program_buffer,
            readback_buffer,
            NULL,
            NULL
        );
        expect_install_status(
            "candidate-ready commit marker failure",
            UPDATE_INSTALL_ERR_METADATA,
            update_installer_install(
                &flash,
                package,
                package_size,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_failure_invariants(&sim, &flash, active);
    }
}

static update_install_status_t enable_corrupt_after_program_hook(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
);

static void test_update_installer_detects_post_program_corruption(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    (void)candidate;
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        enable_corrupt_after_program_hook,
        &sim
    );
    expect_install_status(
        "read-back mismatch after programming",
        UPDATE_INSTALL_ERR_PROGRAM,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
}

static update_install_status_t enable_corrupt_after_program_hook(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
)
{
    simulated_flash_t *sim = (simulated_flash_t *)context;

    if ((sim != NULL) &&
        (point == UPDATE_INSTALL_FAULT_PROGRAM_BLOCK) &&
        (detail == 0U)) {
        simulated_flash_corrupt_after_program(sim, 1U);
    }

    return UPDATE_INSTALL_OK;
}

typedef struct {
    simulated_flash_t *sim;
    uint32_t corrupt_address;
} corrupt_hash_read_context_t;

static update_install_status_t corrupt_hash_read_hook(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
)
{
    corrupt_hash_read_context_t *config =
        (corrupt_hash_read_context_t *)context;

    if ((config != NULL) &&
        (point == UPDATE_INSTALL_FAULT_HASH_BEGIN)) {
        (void)detail;
        simulated_flash_corrupt_read_address(config->sim, config->corrupt_address);
    }

    return UPDATE_INSTALL_OK;
}

typedef struct {
    uint32_t signature_address;
} corrupt_installed_verify_context_t;

static update_install_status_t corrupt_installed_verify_hook(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
)
{
    const corrupt_installed_verify_context_t *config =
        (const corrupt_installed_verify_context_t *)context;
    (void)detail;

    if ((config != NULL) &&
        (point == UPDATE_INSTALL_FAULT_VERIFY_INSTALLED)) {
        uint8_t *signature = (uint8_t *)(uintptr_t)config->signature_address;
        signature[0] ^= 0x01U;
    }

    return UPDATE_INSTALL_OK;
}

typedef struct {
    uint32_t manifest_address;
    uint32_t signature_address;
    uint32_t payload_base;
    size_t payload_size;
} rewrite_manifest_context_t;

static update_install_status_t rewrite_installed_manifest_hook(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
)
{
    rewrite_manifest_context_t *config = (rewrite_manifest_context_t *)context;
    (void)detail;

    if ((config != NULL) &&
        (point == UPDATE_INSTALL_FAULT_VERIFY_INSTALLED)) {
        uint8_t *manifest = (uint8_t *)(uintptr_t)config->manifest_address;
        uint8_t *signature = (uint8_t *)(uintptr_t)config->signature_address;
        const uint8_t *payload = (const uint8_t *)(uintptr_t)config->payload_base;
        uint8_t payload_hash[SIGNED_PAYLOAD_HASH_SIZE];

        crypto_sha512(payload_hash, payload, config->payload_size);
        test_store_le32(
            &manifest[offsetof(signed_manifest_t, image_size)],
            (uint32_t)config->payload_size
        );
        memcpy(
            &manifest[offsetof(signed_manifest_t, payload_sha512)],
            payload_hash,
            sizeof(payload_hash)
        );
        crypto_ed25519_sign(
            signature,
            update_secret_key,
            manifest,
            SIGNED_MANIFEST_SIZE
        );
    }

    return UPDATE_INSTALL_OK;
}

static void test_update_installer_detects_final_hash_mismatch(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    corrupt_hash_read_context_t context = {
        .sim = &sim,
        .corrupt_address = candidate->payload_base,
    };
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        corrupt_hash_read_hook,
        &context
    );
    expect_install_status(
        "final hash mismatch",
        UPDATE_INSTALL_ERR_HASH,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
}

static void test_update_installer_rejects_manifest_size_swap_after_hash(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    update_install_result_t result;

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    rewrite_manifest_context_t context = {
        .manifest_address = candidate->manifest_address,
        .signature_address = candidate->signature_address,
        .payload_base = candidate->payload_base,
        .payload_size = APPLICATION_MIN_SIZE,
    };
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        rewrite_installed_manifest_hook,
        &context
    );

    expect_install_status(
        "valid manifest size swap rejected",
        UPDATE_INSTALL_ERR_INSTALLED_VERIFY,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            &result
        )
    );
    expect_verify_status(
        "manifest size swap verify status",
        VERIFY_BAD_SIGNATURE,
        result.installed_verify_status
    );
    expect_install_failure_invariants(&sim, &flash, active);
}

static void test_update_installer_detects_verifier_failure_after_programming(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    corrupt_installed_verify_context_t context = {
        .signature_address = candidate->signature_address,
    };
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        corrupt_installed_verify_hook,
        &context
    );
    expect_install_status(
        "verifier failure after programming",
        UPDATE_INSTALL_ERR_INSTALLED_VERIFY,
        update_installer_install(
            &flash,
            package,
            package_size,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
}

static void test_update_installer_streaming_success_block_sizes(void)
{
    const size_t block_sizes[] = {1U, 64U, 512U, 1024U};

    for (size_t i = 0U; i < sizeof(block_sizes) / sizeof(block_sizes[0]); ++i) {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_LARGE_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
        uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
        update_install_result_t result;

        const size_t package_size = setup_install_with_payload_size(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            TEST_LARGE_UPDATE_PAYLOAD_SIZE,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        update_install_options_t options = make_install_options_with_sizes(
            program_buffer,
            sizeof(program_buffer),
            readback_buffer,
            sizeof(readback_buffer),
            NULL,
            NULL
        );

        expect_u32(
            "stream package exceeds program buffer",
            1U,
            (package_size > sizeof(program_buffer)) ? 1U : 0U
        );
        expect_u32(
            "stream package exceeds readback buffer",
            1U,
            (package_size > sizeof(readback_buffer)) ? 1U : 0U
        );
        expect_install_status(
            "streaming valid package",
            UPDATE_INSTALL_OK,
            stream_package_with_block_size(
                &flash,
                package,
                package_size,
                block_sizes[i],
                update_public_key,
                &options,
                &result
            )
        );
        expect_u32("stream active slot", BOOT_SLOT_A, result.active_slot);
        expect_u32("stream candidate slot", BOOT_SLOT_B, result.candidate_slot);
        expect_u32("stream image version", 3U, result.image_version);
        expect_u32("stream program blocks", 5U, result.programmed_block_count);
        expect_verify_status("stream header verify", VERIFY_OK, result.package_verify_status);
        expect_verify_status("stream installed verify", VERIFY_OK, result.installed_verify_status);
        expect_protected_regions_unchanged(&sim, active);
        expect_no_forbidden_writes(&sim, active);
        (void)candidate;
    }
}

static void test_update_installer_streaming_updates_b_to_a(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    update_install_result_t result;

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_B,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        NULL,
        NULL
    );

    expect_install_status(
        "streaming update B to A",
        UPDATE_INSTALL_OK,
        stream_package_with_block_size(
            &flash,
            package,
            package_size,
            64U,
            update_public_key,
            &options,
            &result
        )
    );
    expect_u32("stream B active", BOOT_SLOT_B, result.active_slot);
    expect_u32("stream A candidate", BOOT_SLOT_A, result.candidate_slot);
    expect_protected_regions_unchanged(&sim, active);
    expect_no_forbidden_writes(&sim, active);
    (void)candidate;
}

static void test_update_installer_streaming_rejects_header_errors(void)
{
    {
        simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
        uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
        update_installer_session_t session;

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        const size_t writes_before = sim.write_log_count;
        update_install_options_t options = make_install_options_with_sizes(
            program_buffer,
            sizeof(program_buffer),
            readback_buffer,
            sizeof(readback_buffer),
            NULL,
            NULL
        );

        expect_install_status(
            "stream init for truncated header",
            UPDATE_INSTALL_OK,
            update_installer_session_init(
                &session,
                &flash,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_status(
            "stream truncated header rejected",
            UPDATE_INSTALL_ERR_PACKAGE,
            update_installer_begin(
                &session,
                package,
                (size_t)SIGNED_IMAGE_HEADER_SIZE - 1U
            )
        );
        expect_u32("truncated header caused no write", (uint32_t)writes_before, (uint32_t)sim.write_log_count);
        expect_install_failure_invariants(&sim, &flash, active);
        (void)package_size;
        (void)candidate;
    }

    {
        simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
        uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
        update_installer_session_t session;

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        const size_t writes_before = sim.write_log_count;
        update_install_options_t options = make_install_options_with_sizes(
            program_buffer,
            sizeof(program_buffer),
            readback_buffer,
            sizeof(readback_buffer),
            NULL,
            NULL
        );

        package[SIGNED_MANIFEST_SIZE + SIGNED_SIGNATURE_SIZE] = 0x00U;
        expect_install_status(
            "stream init for bad padding",
            UPDATE_INSTALL_OK,
            update_installer_session_init(
                &session,
                &flash,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_status(
            "stream bad padding rejected",
            UPDATE_INSTALL_ERR_PACKAGE,
            update_installer_begin(
                &session,
                package,
                (size_t)SIGNED_IMAGE_HEADER_SIZE
            )
        );
        expect_u32("bad padding caused no write", (uint32_t)writes_before, (uint32_t)sim.write_log_count);
        expect_install_failure_invariants(&sim, &flash, active);
        (void)package_size;
        (void)candidate;
    }

    {
        simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
        uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
        update_installer_session_t session;

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            2U,
            3U,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        const size_t writes_before = sim.write_log_count;
        update_install_options_t options = make_install_options_with_sizes(
            program_buffer,
            sizeof(program_buffer),
            readback_buffer,
            sizeof(readback_buffer),
            NULL,
            NULL
        );

        package[SIGNED_MANIFEST_SIZE] ^= 0x01U;
        expect_install_status(
            "stream init for bad signature",
            UPDATE_INSTALL_OK,
            update_installer_session_init(
                &session,
                &flash,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_status(
            "stream bad signature rejected",
            UPDATE_INSTALL_ERR_PACKAGE,
            update_installer_begin(
                &session,
                package,
                (size_t)SIGNED_IMAGE_HEADER_SIZE
            )
        );
        expect_u32("bad signature caused no write", (uint32_t)writes_before, (uint32_t)sim.write_log_count);
        expect_install_failure_invariants(&sim, &flash, active);
        (void)package_size;
        (void)candidate;
    }
}

static void test_update_installer_streaming_rejects_versions_before_write(void)
{
    for (uint32_t package_version = 2U; package_version <= 3U; ++package_version) {
        simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
        uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
        update_installer_session_t session;

        const size_t package_size = setup_standard_install(
            &sim,
            &flash,
            BOOT_SLOT_A,
            3U,
            package_version,
            package,
            sizeof(package),
            &active,
            &candidate
        );
        const size_t writes_before = sim.write_log_count;
        update_install_options_t options = make_install_options_with_sizes(
            program_buffer,
            sizeof(program_buffer),
            readback_buffer,
            sizeof(readback_buffer),
            NULL,
            NULL
        );

        expect_install_status(
            "stream init for rollback",
            UPDATE_INSTALL_OK,
            update_installer_session_init(
                &session,
                &flash,
                update_public_key,
                &options,
                NULL
            )
        );
        expect_install_status(
            "stream rollback rejected",
            UPDATE_INSTALL_ERR_ROLLBACK,
            update_installer_begin(
                &session,
                package,
                (size_t)SIGNED_IMAGE_HEADER_SIZE
            )
        );
        expect_u32("rollback caused no write", (uint32_t)writes_before, (uint32_t)sim.write_log_count);
        expect_install_failure_invariants(&sim, &flash, active);
        (void)package_size;
        (void)candidate;
    }
}

static void test_update_installer_streaming_detects_bad_payload_hash(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
    uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options_with_sizes(
        program_buffer,
        sizeof(program_buffer),
        readback_buffer,
        sizeof(readback_buffer),
        NULL,
        NULL
    );

    package[SIGNED_IMAGE_HEADER_SIZE + 16U] ^= 0x01U;
    expect_install_status(
        "stream bad payload hash rejected",
        UPDATE_INSTALL_ERR_HASH,
        stream_package_with_block_size(
            &flash,
            package,
            package_size,
            64U,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
    (void)candidate;
}

static void begin_streaming_sequence_test(
    simulated_flash_t *sim,
    boot_flash_t *flash,
    update_installer_session_t *session,
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE],
    const boot_slot_descriptor_t **active,
    const boot_slot_descriptor_t **candidate,
    update_install_options_t *options
)
{
    static uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
    static uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
    const size_t package_size = setup_standard_install(
        sim,
        flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        TEST_PACKAGE_BUFFER_SIZE,
        active,
        candidate
    );

    *options = make_install_options_with_sizes(
        program_buffer,
        sizeof(program_buffer),
        readback_buffer,
        sizeof(readback_buffer),
        NULL,
        NULL
    );
    expect_install_status(
        "stream sequence init",
        UPDATE_INSTALL_OK,
        update_installer_session_init(
            session,
            flash,
            update_public_key,
            options,
            NULL
        )
    );
    expect_install_status(
        "stream sequence begin",
        UPDATE_INSTALL_OK,
        update_installer_begin(
            session,
            package,
            (size_t)SIGNED_IMAGE_HEADER_SIZE
        )
    );
    (void)package_size;
}

static void test_update_installer_streaming_rejects_sequence_errors(void)
{
    {
        simulated_flash_t sim;
        boot_flash_t flash;
        update_installer_session_t session;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        update_install_options_t options;

        begin_streaming_sequence_test(
            &sim,
            &flash,
            &session,
            package,
            &active,
            &candidate,
            &options
        );
        expect_install_status(
            "stream skipped first block rejected",
            UPDATE_INSTALL_ERR_SEQUENCE,
            update_installer_write(
                &session,
                1U,
                &package[SIGNED_IMAGE_HEADER_SIZE],
                1U
            )
        );
        expect_install_failure_invariants(&sim, &flash, active);
        (void)candidate;
    }

    {
        simulated_flash_t sim;
        boot_flash_t flash;
        update_installer_session_t session;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        update_install_options_t options;

        begin_streaming_sequence_test(
            &sim,
            &flash,
            &session,
            package,
            &active,
            &candidate,
            &options
        );
        expect_install_status(
            "stream first block accepted",
            UPDATE_INSTALL_OK,
            update_installer_write(
                &session,
                0U,
                &package[SIGNED_IMAGE_HEADER_SIZE],
                64U
            )
        );
        expect_install_status(
            "stream duplicate block rejected",
            UPDATE_INSTALL_ERR_SEQUENCE,
            update_installer_write(
                &session,
                0U,
                &package[SIGNED_IMAGE_HEADER_SIZE],
                64U
            )
        );
        expect_install_failure_invariants(&sim, &flash, active);
        (void)candidate;
    }

    {
        simulated_flash_t sim;
        boot_flash_t flash;
        update_installer_session_t session;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        update_install_options_t options;

        begin_streaming_sequence_test(
            &sim,
            &flash,
            &session,
            package,
            &active,
            &candidate,
            &options
        );
        expect_install_status(
            "stream too much data rejected",
            UPDATE_INSTALL_ERR_SEQUENCE,
            update_installer_write(
                &session,
                0U,
                &package[SIGNED_IMAGE_HEADER_SIZE],
                TEST_UPDATE_PAYLOAD_SIZE + 1U
            )
        );
        expect_install_failure_invariants(&sim, &flash, active);
        (void)candidate;
    }

    {
        simulated_flash_t sim;
        boot_flash_t flash;
        update_installer_session_t session;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
        update_install_options_t options;

        begin_streaming_sequence_test(
            &sim,
            &flash,
            &session,
            package,
            &active,
            &candidate,
            &options
        );
        expect_install_status(
            "stream partial block accepted",
            UPDATE_INSTALL_OK,
            update_installer_write(
                &session,
                0U,
                &package[SIGNED_IMAGE_HEADER_SIZE],
                64U
            )
        );
        expect_install_status(
            "stream premature finish rejected",
            UPDATE_INSTALL_ERR_SEQUENCE,
            update_installer_finish(&session)
        );
        expect_install_failure_invariants(&sim, &flash, active);
        (void)candidate;
    }
}

static void test_update_installer_streaming_rejects_invalid_api_states(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    update_installer_session_t session;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
    uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
    update_install_options_t options;

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    options = make_install_options_with_sizes(
        program_buffer,
        sizeof(program_buffer),
        readback_buffer,
        sizeof(readback_buffer),
        NULL,
        NULL
    );

    memset(&session, 0, sizeof(session));
    expect_install_status(
        "stream finish before begin rejected",
        UPDATE_INSTALL_ERR_STATE,
        update_installer_finish(&session)
    );
    expect_install_status(
        "stream init for invalid state",
        UPDATE_INSTALL_OK,
        update_installer_session_init(
            &session,
            &flash,
            update_public_key,
            &options,
            NULL
        )
    );

    {
        boot_metadata_record_t current;
        boot_metadata_record_t writing;

        expect_metadata_status(
            "recover metadata before invalid begin",
            BOOT_METADATA_OK,
            boot_metadata_recover_from_flash(&flash, &current, NULL)
        );
        writing = metadata_writing(
            &current,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U
        );
        expect_metadata_status(
            "commit invalid begin writing",
            BOOT_METADATA_OK,
            boot_metadata_commit(&flash, &writing)
        );
    }

    expect_install_status(
        "stream begin after metadata changed rejected",
        UPDATE_INSTALL_ERR_ACTIVE_STATE,
        update_installer_begin(
            &session,
            package,
            (size_t)SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
    (void)package_size;
    (void)candidate;
}

static void test_update_installer_streaming_abort(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    update_installer_session_t session;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
    uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
    boot_metadata_record_t recovered;

    const size_t package_size = setup_streaming_install_with_bootable_active(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options_with_sizes(
        program_buffer,
        sizeof(program_buffer),
        readback_buffer,
        sizeof(readback_buffer),
        NULL,
        NULL
    );

    expect_install_status(
        "stream abort init",
        UPDATE_INSTALL_OK,
        update_installer_session_init(
            &session,
            &flash,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_status(
        "stream abort begin",
        UPDATE_INSTALL_OK,
        update_installer_begin(
            &session,
            package,
            (size_t)SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_install_status(
        "stream abort partial write",
        UPDATE_INSTALL_OK,
        update_installer_write(
            &session,
            0U,
            &package[SIGNED_IMAGE_HEADER_SIZE],
            64U
        )
    );
    expect_install_status(
        "stream abort",
        UPDATE_INSTALL_OK,
        update_installer_abort(&session)
    );
    expect_install_status(
        "stream write after abort rejected",
        UPDATE_INSTALL_ERR_STATE,
        update_installer_write(
            &session,
            64U,
            &package[SIGNED_IMAGE_HEADER_SIZE + 64U],
            64U
        )
    );
    expect_metadata_status(
        "recover rejected abort",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "abort rejected metadata",
        &recovered,
        3U,
        BOOT_METADATA_STATE_REJECTED_INVALID,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );
    expect_reset_selects_active("reset after stream abort", &flash, active);
    expect_install_failure_invariants(&sim, &flash, active);
    (void)package_size;
    (void)candidate;
}

static void test_update_installer_streaming_write_after_finish_rejected(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    update_installer_session_t session;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_SMALL_INSTALL_CHUNK];
    uint8_t readback_buffer[TEST_SMALL_INSTALL_CHUNK];
    size_t payload_offset = 0U;

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options_with_sizes(
        program_buffer,
        sizeof(program_buffer),
        readback_buffer,
        sizeof(readback_buffer),
        NULL,
        NULL
    );

    expect_install_status(
        "stream finish init",
        UPDATE_INSTALL_OK,
        update_installer_session_init(
            &session,
            &flash,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_status(
        "stream finish begin",
        UPDATE_INSTALL_OK,
        update_installer_begin(
            &session,
            package,
            (size_t)SIGNED_IMAGE_HEADER_SIZE
        )
    );
    while (payload_offset < TEST_UPDATE_PAYLOAD_SIZE) {
        expect_install_status(
            "stream finish write",
            UPDATE_INSTALL_OK,
            update_installer_write(
                &session,
                payload_offset,
                &package[SIGNED_IMAGE_HEADER_SIZE + payload_offset],
                64U
            )
        );
        payload_offset += 64U;
    }
    expect_install_status(
        "stream finish",
        UPDATE_INSTALL_OK,
        update_installer_finish(&session)
    );
    expect_install_status(
        "stream write after finish rejected",
        UPDATE_INSTALL_ERR_STATE,
        update_installer_write(
            &session,
            TEST_UPDATE_PAYLOAD_SIZE,
            &package[SIGNED_IMAGE_HEADER_SIZE],
            1U
        )
    );
    expect_protected_regions_unchanged(&sim, active);
    expect_no_forbidden_writes(&sim, active);
    (void)package_size;
    (void)candidate;
}

static void run_streaming_with_hook_failure(
    const char *name,
    update_install_fault_point_t point,
    uint32_t detail,
    uint8_t match_detail
)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    install_fault_config_t fault = {point, detail, match_detail};

    const size_t package_size = setup_standard_install(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        install_fault_hook,
        &fault
    );

    expect_install_status(
        name,
        UPDATE_INSTALL_ERR_INJECTED,
        stream_package_with_block_size(
            &flash,
            package,
            package_size,
            64U,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_failure_invariants(&sim, &flash, active);
    (void)candidate;
}

static void test_update_installer_streaming_fault_injection(void)
{
    run_streaming_with_hook_failure(
        "stream metadata WRITING fault",
        UPDATE_INSTALL_FAULT_METADATA_WRITING,
        0U,
        0U
    );
    run_streaming_with_hook_failure(
        "stream erase fault",
        UPDATE_INSTALL_FAULT_ERASE_SECTOR,
        STM32F429_SLOT_B_FIRST_SECTOR,
        1U
    );
    run_streaming_with_hook_failure(
        "stream program fault",
        UPDATE_INSTALL_FAULT_PROGRAM_BLOCK,
        0U,
        1U
    );
    run_streaming_with_hook_failure(
        "stream readback fault",
        UPDATE_INSTALL_FAULT_READBACK,
        0U,
        1U
    );
    run_streaming_with_hook_failure(
        "stream hash-begin fault",
        UPDATE_INSTALL_FAULT_HASH_BEGIN,
        3U,
        1U
    );
    run_streaming_with_hook_failure(
        "stream hash-complete fault",
        UPDATE_INSTALL_FAULT_HASH_COMPLETE,
        3U,
        1U
    );
    run_streaming_with_hook_failure(
        "stream verify-installed fault",
        UPDATE_INSTALL_FAULT_VERIFY_INSTALLED,
        3U,
        1U
    );
    run_streaming_with_hook_failure(
        "stream candidate-ready fault",
        UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY,
        3U,
        1U
    );
}

static void run_streaming_reset_after_hook_failure(
    const char *name,
    update_install_fault_point_t point,
    uint32_t detail,
    uint8_t match_detail
)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    install_fault_config_t fault = {point, detail, match_detail};

    const size_t package_size = setup_streaming_install_with_bootable_active(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        install_fault_hook,
        &fault
    );

    expect_install_status(
        name,
        UPDATE_INSTALL_ERR_INJECTED,
        stream_package_with_block_size(
            &flash,
            package,
            package_size,
            64U,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_reset_selects_active(name, &flash, active);
    expect_install_failure_invariants(&sim, &flash, active);
    (void)candidate;
}

static void test_update_installer_streaming_reset_after_faults(void)
{
    run_streaming_reset_after_hook_failure(
        "reset after stream metadata WRITING fault",
        UPDATE_INSTALL_FAULT_METADATA_WRITING,
        0U,
        0U
    );
    run_streaming_reset_after_hook_failure(
        "reset after stream erase fault",
        UPDATE_INSTALL_FAULT_ERASE_SECTOR,
        STM32F429_SLOT_B_FIRST_SECTOR,
        1U
    );
    run_streaming_reset_after_hook_failure(
        "reset after stream program fault",
        UPDATE_INSTALL_FAULT_PROGRAM_BLOCK,
        4U,
        1U
    );
    run_streaming_reset_after_hook_failure(
        "reset after stream readback fault",
        UPDATE_INSTALL_FAULT_READBACK,
        4U,
        1U
    );
    run_streaming_reset_after_hook_failure(
        "reset after stream hash-begin fault",
        UPDATE_INSTALL_FAULT_HASH_BEGIN,
        3U,
        1U
    );
    run_streaming_reset_after_hook_failure(
        "reset after stream hash-complete fault",
        UPDATE_INSTALL_FAULT_HASH_COMPLETE,
        3U,
        1U
    );
    run_streaming_reset_after_hook_failure(
        "reset after stream verify-installed fault",
        UPDATE_INSTALL_FAULT_VERIFY_INSTALLED,
        3U,
        1U
    );
    run_streaming_reset_after_hook_failure(
        "reset after stream candidate-ready fault",
        UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY,
        3U,
        1U
    );
}

static void test_update_installer_streaming_reset_mid_write_falls_back(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    update_installer_session_t session;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t package[TEST_PACKAGE_BUFFER_SIZE];
    uint8_t program_buffer[TEST_INSTALL_PROGRAM_CHUNK];
    uint8_t readback_buffer[TEST_INSTALL_PROGRAM_CHUNK];

    const size_t package_size = setup_streaming_install_with_bootable_active(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        package,
        sizeof(package),
        &active,
        &candidate
    );
    update_install_options_t options = make_install_options(
        program_buffer,
        readback_buffer,
        NULL,
        NULL
    );

    expect_install_status(
        "reset mid-write init",
        UPDATE_INSTALL_OK,
        update_installer_session_init(
            &session,
            &flash,
            update_public_key,
            &options,
            NULL
        )
    );
    expect_install_status(
        "reset mid-write begin",
        UPDATE_INSTALL_OK,
        update_installer_begin(
            &session,
            package,
            (size_t)SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_install_status(
        "reset mid-write payload",
        UPDATE_INSTALL_OK,
        update_installer_write(
            &session,
            0U,
            &package[SIGNED_IMAGE_HEADER_SIZE],
            64U
        )
    );
    expect_reset_selects_active("reset during streaming write", &flash, active);
    expect_install_failure_invariants(&sim, &flash, active);
    (void)package_size;
    (void)candidate;
}

static void test_slot_selection_successful_upgrade_and_confirmation(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;
    boot_confirm_result_t confirmation;
    boot_metadata_record_t recovered;

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );
    commit_candidate_ready_metadata(
        &flash,
        BOOT_SLOT_A,
        2U,
        BOOT_SLOT_B,
        3U
    );

    boot_slot_selection_options_t options = make_selection_options(
        &flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );
    expect_selection_status(
        "candidate ready selects trial",
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32("trial decision", BOOT_SLOT_SELECTION_DECISION_TRIAL, selection.decision);
    expect_u32("trial selected candidate", candidate->id, selection.selected_slot);
    expect_u32(
        "trial attempts decremented",
        BOOT_METADATA_MAX_BOOT_ATTEMPTS - 1UL,
        selection.attempts_remaining_after
    );
    expect_verify_status("trial verify status", VERIFY_OK, selection.selected_verify_status);

    expect_metadata_status(
        "recover pending after trial select",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "pending trial metadata",
        &recovered,
        4U,
        BOOT_METADATA_STATE_PENDING_TRIAL,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );
    expect_u32(
        "pending attempts remaining",
        BOOT_METADATA_MAX_BOOT_ATTEMPTS - 1UL,
        recovered.boot_attempt_count
    );

    expect_confirm_status(
        "confirm running candidate",
        BOOT_CONFIRM_OK,
        boot_confirm_current_slot(&flash, BOOT_SLOT_B, &confirmation)
    );
    expect_u32("confirmed slot B", BOOT_SLOT_B, confirmation.confirmed_slot);
    expect_u32("confirmed version", 3U, confirmation.image_version);

    expect_metadata_status(
        "recover confirmed after app confirmation",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "confirmed candidate metadata",
        &recovered,
        5U,
        BOOT_METADATA_STATE_CONFIRMED,
        BOOT_SLOT_B,
        BOOT_SLOT_NONE,
        3U
    );

    expect_confirm_status(
        "confirmation is idempotent",
        BOOT_CONFIRM_OK,
        boot_confirm_current_slot(&flash, BOOT_SLOT_B, &confirmation)
    );
    expect_u32("idempotent confirmation", 1U, confirmation.already_confirmed);

    expect_selection_status(
        "confirmed new slot boots",
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32("confirmed decision", BOOT_SLOT_SELECTION_DECISION_CONFIRMED, selection.decision);
    expect_u32("confirmed selected slot B", BOOT_SLOT_B, selection.selected_slot);
    (void)active;
}

static void test_slot_selection_missing_confirmation_exhausts_attempts(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;
    boot_metadata_record_t recovered;

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );
    commit_candidate_ready_metadata(
        &flash,
        BOOT_SLOT_A,
        2U,
        BOOT_SLOT_B,
        3U
    );
    boot_slot_selection_options_t options = make_selection_options(
        &flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );

    for (uint32_t expected_after = BOOT_METADATA_MAX_BOOT_ATTEMPTS - 1UL;
         expected_after != UINT32_MAX;
         --expected_after) {
        expect_selection_status(
            "repeated reset trial selection",
            BOOT_SLOT_SELECTION_OK,
            boot_slot_selection_select(&options, &selection)
        );
        expect_u32("trial selected slot", candidate->id, selection.selected_slot);
        expect_u32("trial decision while attempts remain", BOOT_SLOT_SELECTION_DECISION_TRIAL, selection.decision);
        expect_u32("attempt counter after reset", expected_after, selection.attempts_remaining_after);
        if (expected_after == 0U) {
            break;
        }
    }

    expect_selection_status(
        "attempt exhaustion falls back",
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32("fallback decision after exhaustion", BOOT_SLOT_SELECTION_DECISION_FALLBACK, selection.decision);
    expect_u32("fallback selected active", active->id, selection.selected_slot);

    expect_metadata_status(
        "recover rejected after exhaustion",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "exhausted trial rejected",
        &recovered,
        7U,
        BOOT_METADATA_STATE_REJECTED_INVALID,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );
    expect_u32(
        "exhaustion result code",
        BOOT_SLOT_SELECTION_RESULT_ATTEMPTS_EXHAUSTED,
        recovered.result
    );
}

static void test_slot_selection_invalid_candidate_falls_back(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;
    boot_metadata_record_t recovered;

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );
    commit_candidate_ready_metadata(
        &flash,
        BOOT_SLOT_A,
        2U,
        BOOT_SLOT_B,
        3U
    );
    sim.storage[
        flash_offset(
            candidate->signed_image_base +
            SIGNED_IMAGE_HEADER_SIZE +
            APPLICATION_VECTOR_MIN_SIZE
        )
    ] ^= 0x01U;

    boot_slot_selection_options_t options = make_selection_options(
        &flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );
    expect_selection_status(
        "bad candidate falls back",
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32("fallback after bad candidate", BOOT_SLOT_SELECTION_DECISION_FALLBACK, selection.decision);
    expect_u32("fallback selected active after bad candidate", active->id, selection.selected_slot);
    expect_u32("bad candidate fallback cause", BOOT_SLOT_SELECTION_ERR_VERIFY, selection.fallback_cause);
    expect_verify_status(
        "candidate hash failure recorded",
        VERIFY_BAD_PAYLOAD_HASH,
        selection.candidate_verify_status
    );

    expect_metadata_status(
        "recover rejected after bad candidate",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "bad candidate rejected metadata",
        &recovered,
        4U,
        BOOT_METADATA_STATE_REJECTED_INVALID,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );
    expect_u32(
        "bad candidate result code",
        BOOT_SLOT_SELECTION_RESULT_CANDIDATE_VERIFY_FAILED,
        recovered.result
    );
}

static void test_slot_selection_recovers_torn_metadata_copy(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t confirmed_again =
        metadata_confirmed(&confirmed, BOOT_SLOT_A, 2U);

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );
    expect_metadata_status(
        "commit confirmed copy A",
        BOOT_METADATA_OK,
        boot_metadata_commit(&flash, &confirmed)
    );
    expect_metadata_status(
        "commit confirmed copy B",
        BOOT_METADATA_OK,
        boot_metadata_commit(&flash, &confirmed_again)
    );

    sim.storage[flash_offset(STM32F429_BOOT_METADATA_B_BASE)] ^= 0x01U;
    boot_slot_selection_options_t options = make_selection_options(
        &flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );
    expect_selection_status(
        "recover one corrupt metadata copy",
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32("single-copy recovery decision", BOOT_SLOT_SELECTION_DECISION_CONFIRMED, selection.decision);
    expect_u32("single-copy selected active", active->id, selection.selected_slot);
    expect_u32("copy A valid after corruption", 1U, selection.metadata_recovery.copy_a_valid);
    expect_u32("copy B invalid after corruption", 0U, selection.metadata_recovery.copy_b_valid);
    (void)candidate;
}

static void test_slot_selection_rejects_ambiguous_or_invalid_metadata(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed_a =
        metadata_confirmed(&empty, BOOT_SLOT_A, 2U);
    boot_metadata_record_t confirmed_b = confirmed_a;

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );
    confirmed_b.active_slot = BOOT_SLOT_B;
    expect_metadata_status(
        "encode selection ambiguous A",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed_a, copy_a)
    );
    expect_metadata_status(
        "encode selection ambiguous B",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed_b, copy_b)
    );
    memcpy(&sim.storage[flash_offset(STM32F429_BOOT_METADATA_A_BASE)], copy_a, sizeof(copy_a));
    memcpy(&sim.storage[flash_offset(STM32F429_BOOT_METADATA_B_BASE)], copy_b, sizeof(copy_b));

    boot_slot_selection_options_t options = make_selection_options(
        &flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );
    expect_selection_status(
        "ambiguous metadata fails closed",
        BOOT_SLOT_SELECTION_ERR_AMBIGUOUS_METADATA,
        boot_slot_selection_select(&options, &selection)
    );

    make_installer_flash(&sim, &flash);
    expect_metadata_status(
        "encode invalid active slot source",
        BOOT_METADATA_OK,
        boot_metadata_encode(&confirmed_a, copy_a)
    );
    test_store_le32(&copy_a[20], 99UL);
    refresh_metadata_crc(copy_a);
    memcpy(&sim.storage[flash_offset(STM32F429_BOOT_METADATA_A_BASE)], copy_a, sizeof(copy_a));
    expect_selection_status(
        "invalid slot metadata rejected",
        BOOT_SLOT_SELECTION_ERR_METADATA,
        boot_slot_selection_select(&options, &selection)
    );

    make_installer_flash(&sim, &flash);
    expect_selection_status(
        "both metadata copies invalid fails closed",
        BOOT_SLOT_SELECTION_ERR_METADATA,
        boot_slot_selection_select(&options, &selection)
    );
    (void)active;
    (void)candidate;
}

static void test_slot_selection_power_loss_before_attempt_commit_falls_back(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
    selection_verify_context_t verify_context;
    boot_slot_selection_result_t selection;
    boot_metadata_record_t recovered;

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );
    commit_candidate_ready_metadata(
        &flash,
        BOOT_SLOT_A,
        2U,
        BOOT_SLOT_B,
        3U
    );

    simulated_flash_fail_before_program_address(
        &sim,
        STM32F429_BOOT_METADATA_B_BASE + TEST_METADATA_COMMIT_OFFSET
    );
    boot_slot_selection_options_t options = make_selection_options(
        &flash,
        &verify_context,
        image_buffer,
        NULL,
        NULL
    );
    expect_selection_status(
        "pending commit power loss falls back",
        BOOT_SLOT_SELECTION_OK,
        boot_slot_selection_select(&options, &selection)
    );
    expect_u32("power-loss fallback decision", BOOT_SLOT_SELECTION_DECISION_FALLBACK, selection.decision);
    expect_u32("power-loss fallback active", active->id, selection.selected_slot);
    expect_u32("power-loss fallback cause", BOOT_SLOT_SELECTION_ERR_COMMIT, selection.fallback_cause);
    expect_metadata_status(
        "recover after pending commit power loss",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "ready metadata survives pending commit failure",
        &recovered,
        3U,
        BOOT_METADATA_STATE_CANDIDATE_READY,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );
    (void)candidate;
}

static void test_slot_selection_logical_failure_injection(void)
{
    {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
        selection_verify_context_t verify_context;
        boot_slot_selection_result_t selection;
        boot_metadata_record_t recovered;
        selection_fault_config_t fault = {
            BOOT_SLOT_SELECTION_FAULT_METADATA_PENDING_TRIAL,
            BOOT_METADATA_MAX_BOOT_ATTEMPTS - 1UL,
            1U
        };

        make_installer_flash(&sim, &flash);
        setup_selection_images(&sim, BOOT_SLOT_A, 2U, 3U, &active, &candidate);
        commit_candidate_ready_metadata(&flash, BOOT_SLOT_A, 2U, BOOT_SLOT_B, 3U);
        boot_slot_selection_options_t options = make_selection_options(
            &flash,
            &verify_context,
            image_buffer,
            selection_fault_hook,
            &fault
        );
        expect_selection_status(
            "pending transition injection falls back",
            BOOT_SLOT_SELECTION_OK,
            boot_slot_selection_select(&options, &selection)
        );
        expect_u32("pending injection fallback", BOOT_SLOT_SELECTION_DECISION_FALLBACK, selection.decision);
        expect_u32("pending injection cause", BOOT_SLOT_SELECTION_ERR_INJECTED, selection.fallback_cause);
        expect_metadata_status(
            "ready survives pending injection",
            BOOT_METADATA_OK,
            boot_metadata_recover_from_flash(&flash, &recovered, NULL)
        );
        expect_metadata_record(
            "ready after pending injection",
            &recovered,
            3U,
            BOOT_METADATA_STATE_CANDIDATE_READY,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U
        );
        (void)active;
        (void)candidate;
    }

    {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
        selection_verify_context_t verify_context;
        boot_slot_selection_result_t selection;
        boot_metadata_record_t recovered;
        selection_fault_config_t fault = {
            BOOT_SLOT_SELECTION_FAULT_METADATA_ATTEMPT_DECREMENT,
            1U,
            1U
        };

        make_installer_flash(&sim, &flash);
        setup_selection_images(&sim, BOOT_SLOT_A, 2U, 3U, &active, &candidate);
        commit_pending_metadata(&flash, BOOT_SLOT_A, 2U, BOOT_SLOT_B, 3U, 2U);
        boot_slot_selection_options_t options = make_selection_options(
            &flash,
            &verify_context,
            image_buffer,
            selection_fault_hook,
            &fault
        );
        expect_selection_status(
            "attempt decrement injection falls back",
            BOOT_SLOT_SELECTION_OK,
            boot_slot_selection_select(&options, &selection)
        );
        expect_u32("attempt injection fallback", BOOT_SLOT_SELECTION_DECISION_FALLBACK, selection.decision);
        expect_u32("attempt injection cause", BOOT_SLOT_SELECTION_ERR_INJECTED, selection.fallback_cause);
        expect_metadata_status(
            "pending survives attempt injection",
            BOOT_METADATA_OK,
            boot_metadata_recover_from_flash(&flash, &recovered, NULL)
        );
        expect_metadata_record(
            "pending after attempt injection",
            &recovered,
            4U,
            BOOT_METADATA_STATE_PENDING_TRIAL,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U
        );
        expect_u32("attempt count preserved", 2U, recovered.boot_attempt_count);
        (void)active;
        (void)candidate;
    }

    {
        static simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
        selection_verify_context_t verify_context;
        boot_slot_selection_result_t selection;
        selection_fault_config_t fault = {
            BOOT_SLOT_SELECTION_FAULT_VERIFY_CANDIDATE,
            BOOT_SLOT_B,
            1U
        };

        make_installer_flash(&sim, &flash);
        setup_selection_images(&sim, BOOT_SLOT_A, 2U, 3U, &active, &candidate);
        commit_candidate_ready_metadata(&flash, BOOT_SLOT_A, 2U, BOOT_SLOT_B, 3U);
        boot_slot_selection_options_t options = make_selection_options(
            &flash,
            &verify_context,
            image_buffer,
            selection_fault_hook,
            &fault
        );
        expect_selection_status(
            "candidate verification injection falls back",
            BOOT_SLOT_SELECTION_OK,
            boot_slot_selection_select(&options, &selection)
        );
        expect_u32("verify injection fallback", BOOT_SLOT_SELECTION_DECISION_FALLBACK, selection.decision);
        expect_u32("verify injection cause", BOOT_SLOT_SELECTION_ERR_INJECTED, selection.fallback_cause);
        (void)active;
        (void)candidate;
    }

    {
        simulated_flash_t sim;
        boot_flash_t flash;
        const boot_slot_descriptor_t *active = NULL;
        const boot_slot_descriptor_t *candidate = NULL;
        uint8_t image_buffer[TEST_PACKAGE_BUFFER_SIZE];
        selection_verify_context_t verify_context;
        boot_slot_selection_result_t selection;
        boot_metadata_record_t recovered;
        selection_fault_config_t fault = {
            BOOT_SLOT_SELECTION_FAULT_METADATA_REJECT_INVALID,
            BOOT_SLOT_SELECTION_RESULT_CANDIDATE_VERIFY_FAILED,
            1U
        };

        make_installer_flash(&sim, &flash);
        setup_selection_images(&sim, BOOT_SLOT_A, 2U, 3U, &active, &candidate);
        commit_candidate_ready_metadata(&flash, BOOT_SLOT_A, 2U, BOOT_SLOT_B, 3U);
        sim.storage[
            flash_offset(
                candidate->signed_image_base +
                SIGNED_IMAGE_HEADER_SIZE +
                APPLICATION_VECTOR_MIN_SIZE
            )
        ] ^= 0x01U;
        boot_slot_selection_options_t options = make_selection_options(
            &flash,
            &verify_context,
            image_buffer,
            selection_fault_hook,
            &fault
        );
        expect_selection_status(
            "reject-invalid injection still falls back",
            BOOT_SLOT_SELECTION_OK,
            boot_slot_selection_select(&options, &selection)
        );
        expect_u32("reject injection fallback", BOOT_SLOT_SELECTION_DECISION_FALLBACK, selection.decision);
        expect_u32("reject injection selected active", active->id, selection.selected_slot);
        expect_metadata_status(
            "ready survives reject injection",
            BOOT_METADATA_OK,
            boot_metadata_recover_from_flash(&flash, &recovered, NULL)
        );
        expect_metadata_record(
            "ready after reject injection",
            &recovered,
            3U,
            BOOT_METADATA_STATE_CANDIDATE_READY,
            BOOT_SLOT_A,
            BOOT_SLOT_B,
            3U
        );
    }
}

static void test_confirmation_rejects_wrong_slot_and_commit_failure(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    boot_confirm_result_t confirmation;
    boot_metadata_record_t recovered;

    make_installer_flash(&sim, &flash);
    setup_selection_images(
        &sim,
        BOOT_SLOT_A,
        2U,
        3U,
        &active,
        &candidate
    );
    commit_pending_metadata(
        &flash,
        BOOT_SLOT_A,
        2U,
        BOOT_SLOT_B,
        3U,
        BOOT_METADATA_MAX_BOOT_ATTEMPTS - 1UL
    );

    expect_confirm_status(
        "cannot confirm another slot",
        BOOT_CONFIRM_ERR_NOT_PENDING,
        boot_confirm_current_slot(&flash, BOOT_SLOT_A, &confirmation)
    );

    simulated_flash_fail_before_program_address(
        &sim,
        STM32F429_BOOT_METADATA_A_BASE + TEST_METADATA_COMMIT_OFFSET
    );
    expect_confirm_status(
        "confirmation commit failure reported",
        BOOT_CONFIRM_ERR_COMMIT,
        boot_confirm_current_slot(&flash, BOOT_SLOT_B, &confirmation)
    );
    expect_metadata_status(
        "recover pending after confirmation commit failure",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &recovered, NULL)
    );
    expect_metadata_record(
        "pending survives confirmation commit failure",
        &recovered,
        4U,
        BOOT_METADATA_STATE_PENDING_TRIAL,
        BOOT_SLOT_A,
        BOOT_SLOT_B,
        3U
    );
    (void)active;
    (void)candidate;
}

int main(void)
{
    init_update_keys();

    test_sector_lookup_and_bounds();
    test_allowed_erase_policy();
    test_target_flash_stub_fails_closed();
    test_target_flash_backend_model_success_and_policy();
    test_target_flash_backend_error_mapping();
    test_target_flash_backend_init_guards();
    test_program_alignment_bounds_and_readback();
    test_read_rejects_address_overflow_before_backend();
    test_readback_verification_failure_is_reported();
    test_failure_injection_is_visible();
    test_flash_init_rejects_invalid_policy();
    test_metadata_encode_decode_and_recovery();
    test_metadata_rejects_noncanonical_records();
    test_metadata_identical_sequence_is_deterministic();
    test_metadata_ambiguous_and_sequence_handling();
    test_metadata_state_transitions();
    test_metadata_flash_commit_and_recovery();
    test_metadata_power_loss_during_copy_write_keeps_previous_copy();
    test_metadata_uncommitted_record_is_not_selected();
    test_metadata_commit_failure_boundaries_are_deterministic();
    test_blank_device_can_be_factory_provisioned_and_boots_slot_a();
    test_update_package_parse_and_verify_security_cases();
    test_update_installer_success_to_candidate_ready();
    test_update_installer_installs_package_larger_than_io_buffers();
    test_update_installer_uses_opposite_inactive_slot();
    test_update_installer_rejects_rollback_and_active_slot_package();
    test_update_installer_rejects_bad_metadata_states();
    test_update_installer_logical_failure_injection();
    test_update_installer_flash_failure_injection();
    test_update_installer_detects_post_program_corruption();
    test_update_installer_detects_final_hash_mismatch();
    test_update_installer_rejects_manifest_size_swap_after_hash();
    test_update_installer_detects_verifier_failure_after_programming();
    test_update_installer_streaming_success_block_sizes();
    test_update_installer_streaming_updates_b_to_a();
    test_update_installer_streaming_rejects_header_errors();
    test_update_installer_streaming_rejects_versions_before_write();
    test_update_installer_streaming_detects_bad_payload_hash();
    test_update_installer_streaming_rejects_sequence_errors();
    test_update_installer_streaming_rejects_invalid_api_states();
    test_update_installer_streaming_abort();
    test_update_installer_streaming_write_after_finish_rejected();
    test_update_installer_streaming_fault_injection();
    test_update_installer_streaming_reset_after_faults();
    test_update_installer_streaming_reset_mid_write_falls_back();

    test_slot_selection_successful_upgrade_and_confirmation();
    test_slot_selection_missing_confirmation_exhausts_attempts();
    test_slot_selection_invalid_candidate_falls_back();
    test_slot_selection_recovers_torn_metadata_copy();
    test_slot_selection_rejects_ambiguous_or_invalid_metadata();
    test_slot_selection_power_loss_before_attempt_commit_falls_back();
    test_slot_selection_logical_failure_injection();
    test_confirmation_rejects_wrong_slot_and_commit_failure();

    if (failures != 0) {
        printf("update storage tests failed: %d\n", failures);
        return 1;
    }

    printf("update storage tests passed\n");
    return 0;
}
