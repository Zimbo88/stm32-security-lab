#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "boot_slot.h"
#include "stm32f429_memory_layout.h"

#define BOOT_METADATA_MAGIC 0x314D5442UL
#define BOOT_METADATA_COMMIT0 0xC0DEF00DUL
#define BOOT_METADATA_COMMIT1 0x3F210FF2UL
#define BOOT_METADATA_MAX_BOOT_ATTEMPTS 3UL

typedef enum {
    BOOT_METADATA_STATE_EMPTY = 0,
    BOOT_METADATA_STATE_WRITING = 1,
    BOOT_METADATA_STATE_CANDIDATE_READY = 2,
    BOOT_METADATA_STATE_PENDING_TRIAL = 3,
    BOOT_METADATA_STATE_CONFIRMED = 4,
    BOOT_METADATA_STATE_REJECTED_INVALID = 5
} boot_metadata_state_t;

typedef enum {
    BOOT_METADATA_COPY_NONE = 0,
    BOOT_METADATA_COPY_A = 1,
    BOOT_METADATA_COPY_B = 2
} boot_metadata_copy_t;

typedef enum {
    BOOT_METADATA_OK = 0,
    BOOT_METADATA_ERR_INVALID_ARGUMENT = 1,
    BOOT_METADATA_ERR_BAD_FORMAT = 2,
    BOOT_METADATA_ERR_CRC = 3,
    BOOT_METADATA_ERR_NO_VALID_COPY = 4,
    BOOT_METADATA_ERR_AMBIGUOUS = 5,
    BOOT_METADATA_ERR_BAD_TRANSITION = 6,
    BOOT_METADATA_ERR_SEQUENCE = 7,
    BOOT_METADATA_ERR_FLASH = 8
} boot_metadata_status_t;

typedef struct {
    uint32_t sequence;
    boot_metadata_state_t state;
    uint32_t active_slot;
    uint32_t candidate_slot;
    uint32_t candidate_image_version;
    uint32_t boot_attempt_count;
    uint32_t confirmation_state;
    uint32_t result;
} boot_metadata_record_t;

typedef struct {
    uint8_t copy_a_valid;
    uint8_t copy_b_valid;
    boot_metadata_copy_t selected_copy;
} boot_metadata_recovery_t;

boot_metadata_status_t boot_metadata_empty(boot_metadata_record_t *record);
boot_metadata_status_t boot_metadata_encode(
    const boot_metadata_record_t *record,
    uint8_t output[STM32F429_BOOT_METADATA_RECORD_SIZE]
);
boot_metadata_status_t boot_metadata_decode(
    const uint8_t *input,
    size_t length,
    boot_metadata_record_t *record
);
boot_metadata_status_t boot_metadata_recover(
    const uint8_t *copy_a,
    const uint8_t *copy_b,
    size_t length,
    boot_metadata_record_t *selected,
    boot_metadata_recovery_t *recovery
);
boot_metadata_status_t boot_metadata_validate_transition(
    const boot_metadata_record_t *current,
    const boot_metadata_record_t *next
);
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
);
boot_metadata_status_t boot_metadata_recover_from_flash(
    const boot_flash_t *flash,
    boot_metadata_record_t *selected,
    boot_metadata_recovery_t *recovery
);
boot_metadata_status_t boot_metadata_commit(
    const boot_flash_t *flash,
    const boot_metadata_record_t *next
);
/* Explicit recovery-only reinitialization for an unrecoverable journal. */
boot_metadata_status_t boot_metadata_reinitialize_for_recovery(
    const boot_flash_t *flash,
    const boot_metadata_record_t *next
);
const char *boot_metadata_status_text(boot_metadata_status_t status);
const char *boot_metadata_state_text(boot_metadata_state_t state);

#endif
