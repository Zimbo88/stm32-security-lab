#include "boot_metadata.h"

#include <string.h>

#define METADATA_BODY_SIZE 60U
#define METADATA_CRC_OFFSET 60U
#define METADATA_PADDING_OFFSET 64U
#define METADATA_COMMIT_OFFSET 120U
#define METADATA_COMMIT_SIZE 8U

_Static_assert(
    STM32F429_BOOT_METADATA_RECORD_SIZE == 128UL,
    "boot metadata record size must remain 128 bytes"
);
_Static_assert(
    (METADATA_COMMIT_OFFSET + METADATA_COMMIT_SIZE) ==
        STM32F429_BOOT_METADATA_RECORD_SIZE,
    "metadata commit marker must end the record"
);
_Static_assert(
    (METADATA_COMMIT_OFFSET & 7UL) == 0UL,
    "metadata commit marker must be 8-byte aligned"
);

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
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

static uint32_t load_le32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0]) |
           (((uint32_t)bytes[1]) << 8) |
           (((uint32_t)bytes[2]) << 16) |
           (((uint32_t)bytes[3]) << 24);
}

static void store_le32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFU);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint8_t slot_value_is_valid(uint32_t slot)
{
    return ((slot == (uint32_t)BOOT_SLOT_A) ||
            (slot == (uint32_t)BOOT_SLOT_B) ||
            (slot == BOOT_SLOT_NONE))
        ? 1U
        : 0U;
}

static uint8_t state_value_is_valid(uint32_t state)
{
    return (state <= (uint32_t)BOOT_METADATA_STATE_REJECTED_INVALID) ? 1U : 0U;
}

static uint8_t state_record_is_canonical(const boot_metadata_record_t *record)
{
    if ((record == NULL) ||
        (record->sequence == 0UL) ||
        (record->sequence == UINT32_MAX) ||
        (state_value_is_valid((uint32_t)record->state) == 0U) ||
        (slot_value_is_valid(record->active_slot) == 0U) ||
        (slot_value_is_valid(record->candidate_slot) == 0U) ||
        (record->boot_attempt_count > BOOT_METADATA_MAX_BOOT_ATTEMPTS) ||
        (record->confirmation_state > 1UL)) {
        return 0U;
    }

    switch (record->state) {
    case BOOT_METADATA_STATE_EMPTY:
        return ((record->active_slot == BOOT_SLOT_NONE) &&
                (record->candidate_slot == BOOT_SLOT_NONE) &&
                (record->candidate_image_version == 0UL) &&
                (record->boot_attempt_count == 0UL) &&
                (record->confirmation_state == 0UL))
            ? 1U
            : 0U;

    case BOOT_METADATA_STATE_CONFIRMED:
        return ((record->active_slot != BOOT_SLOT_NONE) &&
                (record->candidate_slot == BOOT_SLOT_NONE) &&
                (record->candidate_image_version != 0UL) &&
                (record->boot_attempt_count == 0UL) &&
                (record->confirmation_state == 1UL))
            ? 1U
            : 0U;

    case BOOT_METADATA_STATE_WRITING:
    case BOOT_METADATA_STATE_CANDIDATE_READY:
    case BOOT_METADATA_STATE_PENDING_TRIAL:
    case BOOT_METADATA_STATE_REJECTED_INVALID:
        if ((record->candidate_slot == BOOT_SLOT_NONE) ||
            (record->candidate_image_version == 0UL) ||
            ((record->active_slot != BOOT_SLOT_NONE) &&
             (record->active_slot == record->candidate_slot))) {
            return 0U;
        }
        if ((record->state != BOOT_METADATA_STATE_PENDING_TRIAL) &&
            (record->boot_attempt_count != 0UL)) {
            return 0U;
        }
        return (record->confirmation_state == 0UL) ? 1U : 0U;

    default:
        return 0U;
    }
}

static uint8_t transition_state_allowed(
    boot_metadata_state_t current,
    boot_metadata_state_t next
)
{
    switch (current) {
    case BOOT_METADATA_STATE_EMPTY:
        return ((next == BOOT_METADATA_STATE_WRITING) ||
                (next == BOOT_METADATA_STATE_CONFIRMED))
            ? 1U
            : 0U;
    case BOOT_METADATA_STATE_WRITING:
        return ((next == BOOT_METADATA_STATE_CANDIDATE_READY) ||
                (next == BOOT_METADATA_STATE_REJECTED_INVALID))
            ? 1U
            : 0U;
    case BOOT_METADATA_STATE_CANDIDATE_READY:
        return ((next == BOOT_METADATA_STATE_PENDING_TRIAL) ||
                (next == BOOT_METADATA_STATE_REJECTED_INVALID))
            ? 1U
            : 0U;
    case BOOT_METADATA_STATE_PENDING_TRIAL:
        return ((next == BOOT_METADATA_STATE_PENDING_TRIAL) ||
                (next == BOOT_METADATA_STATE_CONFIRMED) ||
                (next == BOOT_METADATA_STATE_REJECTED_INVALID))
            ? 1U
            : 0U;
    case BOOT_METADATA_STATE_CONFIRMED:
        return ((next == BOOT_METADATA_STATE_WRITING) ||
                (next == BOOT_METADATA_STATE_CONFIRMED))
            ? 1U
            : 0U;
    case BOOT_METADATA_STATE_REJECTED_INVALID:
        return (next == BOOT_METADATA_STATE_WRITING) ? 1U : 0U;
    default:
        return 0U;
    }
}

static uint8_t transition_fields_allowed(
    const boot_metadata_record_t *current,
    const boot_metadata_record_t *next
)
{
    if ((current == NULL) || (next == NULL)) {
        return 0U;
    }

    if ((current->state == BOOT_METADATA_STATE_PENDING_TRIAL) &&
        (next->state == BOOT_METADATA_STATE_PENDING_TRIAL)) {
        if (current->boot_attempt_count == 0UL) {
            return 0U;
        }
        return ((next->active_slot == current->active_slot) &&
                (next->candidate_slot == current->candidate_slot) &&
                (next->candidate_image_version ==
                    current->candidate_image_version) &&
                ((next->boot_attempt_count + 1UL) ==
                    current->boot_attempt_count) &&
                (next->confirmation_state == 0UL))
            ? 1U
            : 0U;
    }

    if ((current->state == BOOT_METADATA_STATE_PENDING_TRIAL) &&
        (next->state == BOOT_METADATA_STATE_CONFIRMED)) {
        return ((next->active_slot == current->candidate_slot) &&
                (next->candidate_slot == BOOT_SLOT_NONE) &&
                (next->candidate_image_version ==
                    current->candidate_image_version) &&
                (next->boot_attempt_count == 0UL) &&
                (next->confirmation_state == 1UL))
            ? 1U
            : 0U;
    }

    if ((current->state == BOOT_METADATA_STATE_CONFIRMED) &&
        (next->state == BOOT_METADATA_STATE_CONFIRMED)) {
        return ((next->active_slot == current->active_slot) &&
                (next->candidate_slot == BOOT_SLOT_NONE) &&
                (next->candidate_image_version ==
                    current->candidate_image_version) &&
                (next->boot_attempt_count == 0UL) &&
                (next->confirmation_state == 1UL))
            ? 1U
            : 0U;
    }

    return 1U;
}

static uint8_t records_are_equal(
    const boot_metadata_record_t *left,
    const boot_metadata_record_t *right
)
{
    if ((left == NULL) || (right == NULL)) {
        return 0U;
    }

    return ((left->sequence == right->sequence) &&
            (left->state == right->state) &&
            (left->active_slot == right->active_slot) &&
            (left->candidate_slot == right->candidate_slot) &&
            (left->candidate_image_version == right->candidate_image_version) &&
            (left->boot_attempt_count == right->boot_attempt_count) &&
            (left->confirmation_state == right->confirmation_state) &&
            (left->result == right->result))
        ? 1U
        : 0U;
}

static void encode_without_crc(
    const boot_metadata_record_t *record,
    uint8_t output[STM32F429_BOOT_METADATA_RECORD_SIZE]
)
{
    memset(output, 0, STM32F429_BOOT_METADATA_RECORD_SIZE);
    store_le32(&output[0], BOOT_METADATA_MAGIC);
    store_le32(&output[4], STM32F429_BOOT_METADATA_FORMAT_VERSION);
    store_le32(&output[8], STM32F429_BOOT_METADATA_RECORD_SIZE);
    store_le32(&output[12], record->sequence);
    store_le32(&output[16], (uint32_t)record->state);
    store_le32(&output[20], record->active_slot);
    store_le32(&output[24], record->candidate_slot);
    store_le32(&output[28], record->candidate_image_version);
    store_le32(&output[32], record->boot_attempt_count);
    store_le32(&output[36], record->confirmation_state);
    store_le32(&output[40], record->result);
}

boot_metadata_status_t boot_metadata_empty(boot_metadata_record_t *record)
{
    if (record == NULL) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    record->sequence = 0UL;
    record->state = BOOT_METADATA_STATE_EMPTY;
    record->active_slot = BOOT_SLOT_NONE;
    record->candidate_slot = BOOT_SLOT_NONE;
    record->candidate_image_version = 0UL;
    record->boot_attempt_count = 0UL;
    record->confirmation_state = 0UL;
    record->result = 0UL;
    return BOOT_METADATA_OK;
}

boot_metadata_status_t boot_metadata_encode(
    const boot_metadata_record_t *record,
    uint8_t output[STM32F429_BOOT_METADATA_RECORD_SIZE]
)
{
    if ((record == NULL) || (output == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (state_record_is_canonical(record) == 0U) {
        return BOOT_METADATA_ERR_BAD_FORMAT;
    }

    encode_without_crc(record, output);
    store_le32(&output[METADATA_CRC_OFFSET], crc32_update(0UL, output, METADATA_BODY_SIZE));
    store_le32(&output[METADATA_COMMIT_OFFSET], BOOT_METADATA_COMMIT0);
    store_le32(&output[METADATA_COMMIT_OFFSET + 4U], BOOT_METADATA_COMMIT1);
    return BOOT_METADATA_OK;
}

boot_metadata_status_t boot_metadata_decode(
    const uint8_t *input,
    size_t length,
    boot_metadata_record_t *record
)
{
    if ((input == NULL) || (record == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (length != STM32F429_BOOT_METADATA_RECORD_SIZE) {
        return BOOT_METADATA_ERR_BAD_FORMAT;
    }

    if ((load_le32(&input[METADATA_COMMIT_OFFSET]) != BOOT_METADATA_COMMIT0) ||
        (load_le32(&input[METADATA_COMMIT_OFFSET + 4U]) != BOOT_METADATA_COMMIT1)) {
        return BOOT_METADATA_ERR_BAD_FORMAT;
    }

    for (size_t i = METADATA_PADDING_OFFSET; i < METADATA_COMMIT_OFFSET; ++i) {
        if (input[i] != 0U) {
            return BOOT_METADATA_ERR_BAD_FORMAT;
        }
    }

    if (load_le32(&input[0]) != BOOT_METADATA_MAGIC ||
        load_le32(&input[4]) != STM32F429_BOOT_METADATA_FORMAT_VERSION ||
        load_le32(&input[8]) != STM32F429_BOOT_METADATA_RECORD_SIZE ||
        load_le32(&input[44]) != 0UL ||
        load_le32(&input[48]) != 0UL ||
        load_le32(&input[52]) != 0UL ||
        load_le32(&input[56]) != 0UL) {
        return BOOT_METADATA_ERR_BAD_FORMAT;
    }

    if (crc32_update(0UL, input, METADATA_BODY_SIZE) !=
        load_le32(&input[METADATA_CRC_OFFSET])) {
        return BOOT_METADATA_ERR_CRC;
    }

    record->sequence = load_le32(&input[12]);
    record->state = (boot_metadata_state_t)load_le32(&input[16]);
    record->active_slot = load_le32(&input[20]);
    record->candidate_slot = load_le32(&input[24]);
    record->candidate_image_version = load_le32(&input[28]);
    record->boot_attempt_count = load_le32(&input[32]);
    record->confirmation_state = load_le32(&input[36]);
    record->result = load_le32(&input[40]);

    return (state_record_is_canonical(record) != 0U)
        ? BOOT_METADATA_OK
        : BOOT_METADATA_ERR_BAD_FORMAT;
}

boot_metadata_status_t boot_metadata_recover(
    const uint8_t *copy_a,
    const uint8_t *copy_b,
    size_t length,
    boot_metadata_record_t *selected,
    boot_metadata_recovery_t *recovery
)
{
    boot_metadata_record_t a;
    boot_metadata_record_t b;
    const boot_metadata_status_t status_a =
        boot_metadata_decode(copy_a, length, &a);
    const boot_metadata_status_t status_b =
        boot_metadata_decode(copy_b, length, &b);
    const uint8_t a_valid = (status_a == BOOT_METADATA_OK) ? 1U : 0U;
    const uint8_t b_valid = (status_b == BOOT_METADATA_OK) ? 1U : 0U;

    if (recovery != NULL) {
        recovery->copy_a_valid = a_valid;
        recovery->copy_b_valid = b_valid;
        recovery->selected_copy = BOOT_METADATA_COPY_NONE;
    }

    if (selected == NULL) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if ((a_valid == 0U) && (b_valid == 0U)) {
        (void)boot_metadata_empty(selected);
        return BOOT_METADATA_ERR_NO_VALID_COPY;
    }

    if ((a_valid != 0U) && (b_valid == 0U)) {
        *selected = a;
        if (recovery != NULL) {
            recovery->selected_copy = BOOT_METADATA_COPY_A;
        }
        return BOOT_METADATA_OK;
    }

    if ((a_valid == 0U) && (b_valid != 0U)) {
        *selected = b;
        if (recovery != NULL) {
            recovery->selected_copy = BOOT_METADATA_COPY_B;
        }
        return BOOT_METADATA_OK;
    }

    if (a.sequence == b.sequence) {
        if (records_are_equal(&a, &b) == 0U) {
            (void)boot_metadata_empty(selected);
            return BOOT_METADATA_ERR_AMBIGUOUS;
        }
        *selected = a;
        if (recovery != NULL) {
            recovery->selected_copy = BOOT_METADATA_COPY_A;
        }
        return BOOT_METADATA_OK;
    }

    if (a.sequence > b.sequence) {
        *selected = a;
        if (recovery != NULL) {
            recovery->selected_copy = BOOT_METADATA_COPY_A;
        }
    } else {
        *selected = b;
        if (recovery != NULL) {
            recovery->selected_copy = BOOT_METADATA_COPY_B;
        }
    }

    return BOOT_METADATA_OK;
}

boot_metadata_status_t boot_metadata_validate_transition(
    const boot_metadata_record_t *current,
    const boot_metadata_record_t *next
)
{
    if ((current == NULL) || (next == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (next->sequence == 0UL ||
        next->sequence == UINT32_MAX ||
        current->sequence == UINT32_MAX ||
        next->sequence != (current->sequence + 1UL)) {
        return BOOT_METADATA_ERR_SEQUENCE;
    }

    if (state_record_is_canonical(next) == 0U) {
        return BOOT_METADATA_ERR_BAD_FORMAT;
    }

    if (transition_state_allowed(current->state, next->state) == 0U) {
        return BOOT_METADATA_ERR_BAD_TRANSITION;
    }

    if (transition_fields_allowed(current, next) == 0U) {
        return BOOT_METADATA_ERR_BAD_TRANSITION;
    }

    return BOOT_METADATA_OK;
}

boot_metadata_status_t boot_metadata_prepare_next(
    const boot_metadata_record_t *current,
    boot_metadata_state_t state,
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t candidate_image_version,
    uint32_t boot_attempt_count,
    uint32_t confirmation_state,
    uint32_t result,
    boot_metadata_record_t *next
)
{
    if ((current == NULL) || (next == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (current->sequence >= (UINT32_MAX - 1UL)) {
        return BOOT_METADATA_ERR_SEQUENCE;
    }

    next->sequence = current->sequence + 1UL;
    next->state = state;
    next->active_slot = active_slot;
    next->candidate_slot = candidate_slot;
    next->candidate_image_version = candidate_image_version;
    next->boot_attempt_count = boot_attempt_count;
    next->confirmation_state = confirmation_state;
    next->result = result;
    return boot_metadata_validate_transition(current, next);
}

boot_metadata_status_t boot_metadata_recover_from_flash(
    const boot_flash_t *flash,
    boot_metadata_record_t *selected,
    boot_metadata_recovery_t *recovery
)
{
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];

    if ((flash == NULL) || (selected == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (boot_flash_read(
            flash,
            STM32F429_BOOT_METADATA_A_BASE,
            copy_a,
            sizeof(copy_a)
        ) != BOOT_FLASH_OK) {
        return BOOT_METADATA_ERR_FLASH;
    }

    if (boot_flash_read(
            flash,
            STM32F429_BOOT_METADATA_B_BASE,
            copy_b,
            sizeof(copy_b)
        ) != BOOT_FLASH_OK) {
        return BOOT_METADATA_ERR_FLASH;
    }

    return boot_metadata_recover(copy_a, copy_b, sizeof(copy_a), selected, recovery);
}

static boot_metadata_status_t write_copy(
    const boot_flash_t *flash,
    boot_metadata_copy_t copy,
    const boot_metadata_record_t *record
)
{
    uint8_t encoded[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint32_t base = STM32F429_BOOT_METADATA_A_BASE;
    uint32_t sector = STM32F429_BOOT_METADATA_A_FIRST_SECTOR;

    if ((flash == NULL) || (record == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (copy == BOOT_METADATA_COPY_B) {
        base = STM32F429_BOOT_METADATA_B_BASE;
        sector = STM32F429_BOOT_METADATA_B_FIRST_SECTOR;
    } else if (copy != BOOT_METADATA_COPY_A) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (boot_metadata_encode(record, encoded) != BOOT_METADATA_OK) {
        return BOOT_METADATA_ERR_BAD_FORMAT;
    }

    if (boot_flash_erase_sector(flash, sector) != BOOT_FLASH_OK) {
        return BOOT_METADATA_ERR_FLASH;
    }

    if (boot_flash_program_aligned(flash, base, encoded, METADATA_COMMIT_OFFSET) !=
        BOOT_FLASH_OK) {
        return BOOT_METADATA_ERR_FLASH;
    }

    if (boot_flash_program_aligned(
            flash,
            base + METADATA_COMMIT_OFFSET,
            &encoded[METADATA_COMMIT_OFFSET],
            METADATA_COMMIT_SIZE
        ) != BOOT_FLASH_OK) {
        return BOOT_METADATA_ERR_FLASH;
    }

    return BOOT_METADATA_OK;
}

boot_metadata_status_t boot_metadata_commit(
    const boot_flash_t *flash,
    const boot_metadata_record_t *next
)
{
    boot_metadata_record_t current;
    boot_metadata_recovery_t recovery;
    boot_metadata_status_t status =
        boot_metadata_recover_from_flash(flash, &current, &recovery);
    boot_metadata_copy_t target = BOOT_METADATA_COPY_A;

    if ((flash == NULL) || (next == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    if (status == BOOT_METADATA_ERR_NO_VALID_COPY) {
        (void)boot_metadata_empty(&current);
    } else if (status != BOOT_METADATA_OK) {
        return status;
    }

    status = boot_metadata_validate_transition(&current, next);
    if (status != BOOT_METADATA_OK) {
        return status;
    }

    if ((recovery.copy_a_valid == 0U) && (recovery.copy_b_valid == 0U)) {
        target = BOOT_METADATA_COPY_A;
    } else if (recovery.copy_a_valid == 0U) {
        target = BOOT_METADATA_COPY_A;
    } else if (recovery.copy_b_valid == 0U) {
        target = BOOT_METADATA_COPY_B;
    } else if (recovery.selected_copy == BOOT_METADATA_COPY_A) {
        target = BOOT_METADATA_COPY_B;
    } else {
        target = BOOT_METADATA_COPY_A;
    }

    return write_copy(flash, target, next);
}

const char *boot_metadata_status_text(boot_metadata_status_t status)
{
    switch (status) {
    case BOOT_METADATA_OK:                   return "OK";
    case BOOT_METADATA_ERR_INVALID_ARGUMENT: return "INVALID ARGUMENT";
    case BOOT_METADATA_ERR_BAD_FORMAT:       return "BAD FORMAT";
    case BOOT_METADATA_ERR_CRC:              return "CRC";
    case BOOT_METADATA_ERR_NO_VALID_COPY:    return "NO VALID COPY";
    case BOOT_METADATA_ERR_AMBIGUOUS:        return "AMBIGUOUS";
    case BOOT_METADATA_ERR_BAD_TRANSITION:   return "BAD TRANSITION";
    case BOOT_METADATA_ERR_SEQUENCE:         return "SEQUENCE";
    case BOOT_METADATA_ERR_FLASH:            return "FLASH";
    default:                                 return "UNKNOWN";
    }
}

const char *boot_metadata_state_text(boot_metadata_state_t state)
{
    switch (state) {
    case BOOT_METADATA_STATE_EMPTY:            return "EMPTY";
    case BOOT_METADATA_STATE_WRITING:          return "WRITING";
    case BOOT_METADATA_STATE_CANDIDATE_READY:  return "CANDIDATE READY";
    case BOOT_METADATA_STATE_PENDING_TRIAL:    return "PENDING TRIAL";
    case BOOT_METADATA_STATE_CONFIRMED:        return "CONFIRMED";
    case BOOT_METADATA_STATE_REJECTED_INVALID: return "REJECTED INVALID";
    default:                                   return "UNKNOWN";
    }
}
