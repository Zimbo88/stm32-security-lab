#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_flash.h"
#include "boot_flash_target.h"
#include "boot_metadata.h"
#include "boot_slot.h"
#include "simulated_flash.h"
#include "stm32f429_memory_layout.h"

static int failures;

#define TEST_METADATA_BODY_SIZE 60U
#define TEST_METADATA_CRC_OFFSET 60U
#define TEST_METADATA_PADDING_OFFSET 64U
#define TEST_METADATA_COMMIT_OFFSET (STM32F429_BOOT_METADATA_RECORD_SIZE - 8U)

static const boot_flash_region_t write_regions[] = {
    {STM32F429_BOOT_METADATA_A_BASE, STM32F429_BOOT_METADATA_A_END},
    {STM32F429_BOOT_METADATA_B_BASE, STM32F429_BOOT_METADATA_B_END},
    {STM32F429_UPDATE_METADATA_BASE, STM32F429_UPDATE_METADATA_END},
    {STM32F429_SLOT_B_SIGNED_IMAGE_BASE, STM32F429_SLOT_B_END},
};

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

    expect_status("sector 12", BOOT_FLASH_OK, boot_flash_sector_by_id(12U, &sector));
    expect_u32("sector 12 base", STM32F429_SLOT_B_SIGNED_IMAGE_BASE, sector.base);
    expect_u32("sector 12 size", 0x00004000UL, sector.size);

    expect_status("sector 23", BOOT_FLASH_OK, boot_flash_sector_by_id(23U, &sector));
    expect_u32("sector 23 base", STM32F429_RECOVERY_BASE, sector.base);
    expect_u32("sector 23 end", STM32F429_FLASH_END, sector.end);

    expect_status(
        "invalid sector",
        BOOT_FLASH_ERR_INVALID_ARGUMENT,
        boot_flash_sector_by_id(24U, &sector)
    );
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
        "pending requires attempts",
        BOOT_METADATA_ERR_BAD_FORMAT,
        boot_metadata_prepare_next(
            &ready,
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

int main(void)
{
    test_sector_lookup_and_bounds();
    test_allowed_erase_policy();
    test_target_flash_stub_fails_closed();
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

    if (failures != 0) {
        printf("update storage tests failed: %d\n", failures);
        return 1;
    }

    printf("update storage tests passed\n");
    return 0;
}
