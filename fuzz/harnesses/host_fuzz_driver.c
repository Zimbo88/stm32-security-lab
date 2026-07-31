#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_flash.h"
#include "boot_metadata.h"
#include "boot_slot_selection.h"
#include "boot_slot.h"
#include "signed_image.h"
#include "stm32f429_memory_layout.h"
#include "update_package.h"
#include "update_protocol.h"

#define FUZZ_MAX_INPUT 65536U
#define METADATA_STORAGE_BASE STM32F429_BOOT_METADATA_A_BASE
#define METADATA_STORAGE_SIZE \
    (STM32F429_BOOT_METADATA_B_END - STM32F429_BOOT_METADATA_A_BASE)

static uint32_t next_random(uint32_t *state)
{
    *state = (uint32_t)((*state * 1664525UL) + 1013904223UL);
    return *state;
}

static int read_input(
    const char *path,
    uint8_t *buffer,
    size_t capacity,
    size_t *length
)
{
    FILE *stream;
    size_t count;

    if ((path == NULL) || (buffer == NULL) || (length == NULL)) {
        return 0;
    }

    stream = fopen(path, "rb");
    if (stream == NULL) {
        fprintf(stderr, "fuzz input: %s: %s\n", path, strerror(errno));
        return 0;
    }
    count = fread(buffer, 1U, capacity, stream);
    if (ferror(stream) != 0) {
        fclose(stream);
        return 0;
    }
    if (fgetc(stream) != EOF) {
        fprintf(stderr, "fuzz input exceeds %zu bytes\n", capacity);
        fclose(stream);
        return 0;
    }
    fclose(stream);
    *length = count;
    return 1;
}

void fuzz_run_uart(const uint8_t *data, size_t length)
{
    update_protocol_parser_t parser;

    update_protocol_parser_init(&parser);
    for (size_t i = 0U; i < length; ++i) {
        const update_protocol_parse_status_t status =
            update_protocol_parser_push(&parser, data[i]);
        if ((status == UPDATE_PROTOCOL_PARSE_ERR_OVERSIZE) ||
            (status == UPDATE_PROTOCOL_PARSE_ERR_CRC)) {
            /* Continue feeding bytes to exercise resynchronisation. */
        }
        if (status == UPDATE_PROTOCOL_PARSE_FRAME_READY) {
            const update_protocol_frame_t *frame =
                update_protocol_parser_frame(&parser);
            if ((frame == NULL) ||
                (frame->payload_length > UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE)) {
                abort();
            }
        }
        (void)update_protocol_parser_has_partial(&parser);
    }
}

void fuzz_run_metadata(const uint8_t *data, size_t length)
{
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];
    boot_metadata_record_t selected;
    boot_metadata_recovery_t recovery;

    memset(copy_a, 0xFF, sizeof(copy_a));
    memset(copy_b, 0xFF, sizeof(copy_b));
    if (length > sizeof(copy_a)) {
        memcpy(copy_a, data, sizeof(copy_a));
        memcpy(copy_b, &data[sizeof(copy_a)],
            (length - sizeof(copy_a) > sizeof(copy_b))
                ? sizeof(copy_b)
                : length - sizeof(copy_a));
    } else {
        memcpy(copy_a, data, length);
    }

    (void)boot_metadata_recover(
        copy_a,
        copy_b,
        sizeof(copy_a),
        &selected,
        &recovery
    );
}

void fuzz_run_package(const uint8_t *data, size_t length)
{
    uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE];
    update_package_t package;
    update_package_header_t header;
    verify_status_t verify_status;
    const boot_slot_descriptor_t *slot = NULL;

    memset(public_key, 0xA5, sizeof(public_key));
    (void)boot_slot_lookup((length != 0U) ? (data[0] & 1U) : 0U, &slot);
    (void)update_package_parse(data, length, &package);
    if ((slot != NULL) && (length >= SIGNED_IMAGE_HEADER_SIZE)) {
        (void)update_package_verify_header_for_slot(
            data,
            SIGNED_IMAGE_HEADER_SIZE,
            public_key,
            slot,
            &header,
            &verify_status
        );
        (void)update_package_verify_for_slot(
            data,
            length,
            public_key,
            slot,
            &package,
            &verify_status
        );
    }
}

typedef struct {
    uint8_t storage[METADATA_STORAGE_SIZE];
} policy_flash_t;

static uint8_t policy_range(
    uint32_t address,
    size_t length,
    size_t *offset
)
{
    if ((offset == NULL) || (address < METADATA_STORAGE_BASE)) {
        return 0U;
    }
    *offset = (size_t)(address - METADATA_STORAGE_BASE);
    return (*offset <= sizeof(((policy_flash_t *)0)->storage) &&
            length <= (sizeof(((policy_flash_t *)0)->storage) - *offset))
        ? 1U
        : 0U;
}

static boot_flash_status_t policy_read(
    void *context,
    uint32_t address,
    uint8_t *output,
    size_t length
)
{
    policy_flash_t *flash = (policy_flash_t *)context;
    size_t offset = 0U;

    if ((flash == NULL) ||
        ((length != 0U) && (output == NULL)) ||
        (policy_range(address, length, &offset) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    memcpy(output, &flash->storage[offset], length);
    return BOOT_FLASH_OK;
}

static boot_flash_status_t policy_erase(
    void *context,
    uint32_t sector_id
)
{
    policy_flash_t *flash = (policy_flash_t *)context;
    boot_flash_sector_t sector;
    size_t offset = 0U;

    if ((flash == NULL) ||
        (boot_flash_sector_by_id(sector_id, &sector) != BOOT_FLASH_OK) ||
        (policy_range(sector.base, sector.size, &offset) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    memset(&flash->storage[offset], 0xFF, sector.size);
    return BOOT_FLASH_OK;
}

static boot_flash_status_t policy_program(
    void *context,
    uint32_t address,
    const uint8_t *data,
    size_t length
)
{
    policy_flash_t *flash = (policy_flash_t *)context;
    size_t offset = 0U;

    if ((flash == NULL) || (data == NULL) || (length == 0U) ||
        (policy_range(address, length, &offset) == 0U)) {
        return BOOT_FLASH_ERR_BACKEND;
    }
    for (size_t i = 0U; i < length; ++i) {
        if ((uint8_t)(flash->storage[offset + i] & data[i]) != data[i]) {
            return BOOT_FLASH_ERR_BACKEND;
        }
    }
    for (size_t i = 0U; i < length; ++i) {
        flash->storage[offset + i] &= data[i];
    }
    return BOOT_FLASH_OK;
}

static const boot_flash_ops_t policy_flash_ops = {
    .read = policy_read,
    .erase_sector = policy_erase,
    .program = policy_program,
};

static const boot_flash_region_t policy_write_regions[] = {
    {STM32F429_BOOT_METADATA_A_BASE, STM32F429_BOOT_METADATA_A_END},
    {STM32F429_BOOT_METADATA_B_BASE, STM32F429_BOOT_METADATA_B_END},
};

static boot_slot_selection_status_t policy_verify_slot(
    void *context,
    const boot_slot_descriptor_t *slot,
    signed_image_jump_context_t *jump_context,
    verify_status_t *verify_status
)
{
    (void)context;
    if ((slot == NULL) || (jump_context == NULL) || (verify_status == NULL)) {
        return BOOT_SLOT_SELECTION_ERR_INVALID_ARGUMENT;
    }
    memset(jump_context, 0, sizeof(*jump_context));
    jump_context->vector_address = slot->payload_base;
    jump_context->image_size = APPLICATION_MIN_SIZE;
    jump_context->payload_end = slot->payload_base + APPLICATION_MIN_SIZE;
    jump_context->initial_msp = APPLICATION_MSP_END;
    jump_context->reset_vector = slot->payload_base | 1UL;
    jump_context->reset_address = slot->payload_base;
    *verify_status = VERIFY_OK;
    return BOOT_SLOT_SELECTION_OK;
}

static boot_slot_selection_status_t policy_expected(
    const uint8_t *copy_a,
    const uint8_t *copy_b,
    boot_slot_selection_decision_t *decision
)
{
    boot_metadata_record_t record;
    boot_metadata_recovery_t recovery;
    const boot_metadata_status_t metadata_status = boot_metadata_recover(
        copy_a,
        copy_b,
        STM32F429_BOOT_METADATA_RECORD_SIZE,
        &record,
        &recovery
    );

    if (metadata_status == BOOT_METADATA_ERR_AMBIGUOUS) {
        return BOOT_SLOT_SELECTION_ERR_AMBIGUOUS_METADATA;
    }
    if (metadata_status != BOOT_METADATA_OK) {
        return BOOT_SLOT_SELECTION_ERR_METADATA;
    }

    if (record.state == BOOT_METADATA_STATE_CONFIRMED) {
        *decision = BOOT_SLOT_SELECTION_DECISION_CONFIRMED;
        return BOOT_SLOT_SELECTION_OK;
    }
    if ((record.state == BOOT_METADATA_STATE_CANDIDATE_READY) ||
        ((record.state == BOOT_METADATA_STATE_PENDING_TRIAL) &&
         (record.boot_attempt_count > 0UL))) {
        *decision = BOOT_SLOT_SELECTION_DECISION_TRIAL;
        return BOOT_SLOT_SELECTION_OK;
    }
    if ((record.active_slot != BOOT_SLOT_NONE) &&
        (record.state != BOOT_METADATA_STATE_EMPTY)) {
        *decision = BOOT_SLOT_SELECTION_DECISION_FALLBACK;
        return BOOT_SLOT_SELECTION_OK;
    }
    return BOOT_SLOT_SELECTION_ERR_NO_BOOTABLE_SLOT;
}

void fuzz_run_policy(const uint8_t *data, size_t length)
{
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];
    policy_flash_t simulated;
    boot_flash_t flash;
    boot_slot_selection_options_t options;
    boot_slot_selection_result_t result;
    boot_slot_selection_decision_t expected_decision =
        BOOT_SLOT_SELECTION_DECISION_NONE;
    boot_slot_selection_status_t expected_status;
    boot_slot_selection_status_t actual_status;
    size_t second_length = 0U;

    memset(copy_a, 0xFF, sizeof(copy_a));
    memset(copy_b, 0xFF, sizeof(copy_b));
    if (length > sizeof(copy_a)) {
        memcpy(copy_a, data, sizeof(copy_a));
        second_length = length - sizeof(copy_a);
        if (second_length > sizeof(copy_b)) {
            second_length = sizeof(copy_b);
        }
        memcpy(copy_b, &data[sizeof(copy_a)], second_length);
    } else {
        memcpy(copy_a, data, length);
    }

    memset(&simulated, 0xFF, sizeof(simulated));
    memcpy(simulated.storage, copy_a, sizeof(copy_a));
    memcpy(
        &simulated.storage[STM32F429_BOOT_METADATA_B_BASE -
            STM32F429_BOOT_METADATA_A_BASE],
        copy_b,
        sizeof(copy_b)
    );
    if (boot_flash_init(
            &flash,
            &simulated,
            &policy_flash_ops,
            policy_write_regions,
            sizeof(policy_write_regions) / sizeof(policy_write_regions[0]),
            8U
        ) != BOOT_FLASH_OK) {
        abort();
    }

    memset(&options, 0, sizeof(options));
    options.metadata_flash = &flash;
    options.verify_slot = policy_verify_slot;
    expected_status = policy_expected(copy_a, copy_b, &expected_decision);
    actual_status = boot_slot_selection_select(&options, &result);
    if ((actual_status != expected_status) ||
        ((actual_status == BOOT_SLOT_SELECTION_OK) &&
         (result.decision != expected_decision))) {
        fprintf(
            stderr,
            "boot policy model mismatch: expected status=%d decision=%d, "
            "actual status=%d decision=%d\n",
            (int)expected_status,
            (int)expected_decision,
            (int)actual_status,
            (int)result.decision
        );
        abort();
    }
}

static void run_one(
    const char *mode,
    const uint8_t *data,
    size_t length
)
{
    if (strcmp(mode, "uart") == 0) {
        fuzz_run_uart(data, length);
    } else if (strcmp(mode, "metadata") == 0) {
        fuzz_run_metadata(data, length);
    } else if (strcmp(mode, "package") == 0) {
        fuzz_run_package(data, length);
    } else if (strcmp(mode, "policy") == 0) {
        fuzz_run_policy(data, length);
    } else {
        fprintf(stderr, "unknown fuzz mode: %s\n", mode);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    uint8_t input[FUZZ_MAX_INPUT];
    uint8_t mutated[FUZZ_MAX_INPUT];
    const char *mode = "uart";
    const char *input_path = NULL;
    size_t input_length = 0U;
    uint64_t iterations = 1000U;
    uint32_t random_state = 0xC0DEC0DEUL;

    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "--mode") == 0) && (i + 1 < argc)) {
            mode = argv[++i];
        } else if ((strcmp(argv[i], "--input") == 0) && (i + 1 < argc)) {
            input_path = argv[++i];
        } else if ((strcmp(argv[i], "--iterations") == 0) && (i + 1 < argc)) {
            iterations = strtoull(argv[++i], NULL, 10);
        } else if ((strcmp(argv[i], "--seed") == 0) && (i + 1 < argc)) {
            random_state = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "usage: %s --mode MODE --input FILE --iterations N\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (input_path != NULL) {
        if (read_input(input_path, input, sizeof(input), &input_length) == 0) {
            return EXIT_FAILURE;
        }
    } else {
        static const uint8_t default_input[] = {
            'S', 'U', 'P', 'D', 1U, UPDATE_PROTOCOL_CMD_HELLO,
            0U, 0U, 0U, 0U, 0x2FU, 0xB2U, 0xE0U, 0xB2U
        };
        memcpy(input, default_input, sizeof(default_input));
        input_length = sizeof(default_input);
    }

    run_one(mode, input, input_length);
    for (uint64_t iteration = 0U; iteration < iterations; ++iteration) {
        size_t mutated_length = input_length;
        memcpy(mutated, input, input_length);
        if (mutated_length != 0U) {
            const size_t changes =
                1U + (size_t)(next_random(&random_state) % 8U);
            for (size_t i = 0U; i < changes; ++i) {
                const size_t index =
                    (size_t)(next_random(&random_state) % mutated_length);
                mutated[index] ^= (uint8_t)next_random(&random_state);
            }
            if ((next_random(&random_state) & 7U) == 0U &&
                mutated_length < sizeof(mutated)) {
                mutated[mutated_length++] = (uint8_t)next_random(&random_state);
            }
            if ((next_random(&random_state) & 15U) == 0U) {
                mutated_length = (size_t)(next_random(&random_state) %
                    (sizeof(mutated) + 1U));
                if (mutated_length > input_length) {
                    memset(
                        &mutated[input_length],
                        (int)(next_random(&random_state) & 0xFFU),
                        mutated_length - input_length
                    );
                }
            }
        }
        run_one(mode, mutated, mutated_length);
    }

    printf(
        "FUZZ_OK mode=%s iterations=%" PRIu64 " seed=0x%08" PRIX32
        " input_bytes=%zu\n",
        mode,
        iterations,
        random_state,
        input_length
    );
    return EXIT_SUCCESS;
}
