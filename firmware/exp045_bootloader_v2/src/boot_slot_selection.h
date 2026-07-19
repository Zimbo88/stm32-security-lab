#ifndef BOOT_SLOT_SELECTION_H
#define BOOT_SLOT_SELECTION_H

#include <stdint.h>

#include "boot_flash.h"
#include "boot_metadata.h"
#include "boot_slot.h"
#include "signed_image.h"

#define BOOT_SLOT_SELECTION_RESULT_NONE 0UL
#define BOOT_SLOT_SELECTION_RESULT_ATTEMPTS_EXHAUSTED 1UL
#define BOOT_SLOT_SELECTION_RESULT_CANDIDATE_VERIFY_FAILED 2UL

typedef enum {
    BOOT_SLOT_SELECTION_OK = 0,
    BOOT_SLOT_SELECTION_ERR_INVALID_ARGUMENT = 1,
    BOOT_SLOT_SELECTION_ERR_METADATA = 2,
    BOOT_SLOT_SELECTION_ERR_AMBIGUOUS_METADATA = 3,
    BOOT_SLOT_SELECTION_ERR_SLOT = 4,
    BOOT_SLOT_SELECTION_ERR_VERIFY = 5,
    BOOT_SLOT_SELECTION_ERR_COMMIT = 6,
    BOOT_SLOT_SELECTION_ERR_NO_BOOTABLE_SLOT = 7,
    BOOT_SLOT_SELECTION_ERR_INJECTED = 8
} boot_slot_selection_status_t;

typedef enum {
    BOOT_SLOT_SELECTION_DECISION_NONE = 0,
    BOOT_SLOT_SELECTION_DECISION_CONFIRMED = 1,
    BOOT_SLOT_SELECTION_DECISION_TRIAL = 2,
    BOOT_SLOT_SELECTION_DECISION_FALLBACK = 3
} boot_slot_selection_decision_t;

typedef enum {
    BOOT_SLOT_SELECTION_FAULT_VERIFY_CANDIDATE = 0,
    BOOT_SLOT_SELECTION_FAULT_VERIFY_CONFIRMED = 1,
    BOOT_SLOT_SELECTION_FAULT_METADATA_PENDING_TRIAL = 2,
    BOOT_SLOT_SELECTION_FAULT_METADATA_ATTEMPT_DECREMENT = 3,
    BOOT_SLOT_SELECTION_FAULT_METADATA_REJECT_INVALID = 4
} boot_slot_selection_fault_point_t;

typedef boot_slot_selection_status_t (*boot_slot_selection_verify_fn_t)(
    void *context,
    const boot_slot_descriptor_t *slot,
    signed_image_jump_context_t *jump_context,
    verify_status_t *verify_status
);

typedef boot_slot_selection_status_t (*boot_slot_selection_fault_hook_t)(
    void *context,
    boot_slot_selection_fault_point_t point,
    uint32_t detail
);

typedef struct {
    const boot_flash_t *metadata_flash;
    boot_slot_selection_verify_fn_t verify_slot;
    void *verify_context;
    boot_slot_selection_fault_hook_t fault_hook;
    void *fault_context;
} boot_slot_selection_options_t;

typedef struct {
    boot_slot_selection_decision_t decision;
    uint32_t selected_slot;
    uint32_t confirmed_slot;
    uint32_t candidate_slot;
    uint32_t image_version;
    uint32_t attempts_remaining_before;
    uint32_t attempts_remaining_after;
    boot_metadata_record_t metadata_before;
    boot_metadata_recovery_t metadata_recovery;
    verify_status_t selected_verify_status;
    verify_status_t candidate_verify_status;
    verify_status_t confirmed_verify_status;
    boot_slot_selection_status_t fallback_cause;
    signed_image_jump_context_t jump_context;
} boot_slot_selection_result_t;

boot_slot_selection_status_t boot_slot_selection_select(
    const boot_slot_selection_options_t *options,
    boot_slot_selection_result_t *result
);
const char *boot_slot_selection_status_text(boot_slot_selection_status_t status);
const char *boot_slot_selection_decision_text(boot_slot_selection_decision_t decision);

#endif
