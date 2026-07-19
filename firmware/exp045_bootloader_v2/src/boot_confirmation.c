#include "boot_confirmation.h"

#include <string.h>

static void init_confirm_result(boot_confirm_result_t *result)
{
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->confirmed_slot = BOOT_SLOT_NONE;
}

boot_confirm_status_t boot_confirm_slot_from_vector_address(
    uint32_t vector_address,
    uint32_t *slot_id
)
{
    const boot_slot_descriptor_t *slot = NULL;

    if (slot_id == NULL) {
        return BOOT_CONFIRM_ERR_INVALID_ARGUMENT;
    }

    *slot_id = BOOT_SLOT_NONE;

    if ((boot_slot_lookup((uint32_t)BOOT_SLOT_A, &slot) ==
            BOOT_SLOT_LOOKUP_OK) &&
        (slot->payload_base == vector_address)) {
        *slot_id = (uint32_t)slot->id;
        return BOOT_CONFIRM_OK;
    }

    if ((boot_slot_lookup((uint32_t)BOOT_SLOT_B, &slot) ==
            BOOT_SLOT_LOOKUP_OK) &&
        (slot->payload_base == vector_address)) {
        *slot_id = (uint32_t)slot->id;
        return BOOT_CONFIRM_OK;
    }

    return BOOT_CONFIRM_ERR_SLOT;
}

boot_confirm_status_t boot_confirm_current_slot(
    const boot_flash_t *flash,
    uint32_t running_slot,
    boot_confirm_result_t *result
)
{
    boot_metadata_record_t metadata;
    boot_metadata_recovery_t recovery;
    boot_metadata_record_t confirmed;
    boot_metadata_status_t metadata_status;
    const boot_slot_descriptor_t *slot = NULL;

    if ((flash == NULL) || (result == NULL)) {
        return BOOT_CONFIRM_ERR_INVALID_ARGUMENT;
    }

    init_confirm_result(result);

    if (boot_slot_lookup(running_slot, &slot) != BOOT_SLOT_LOOKUP_OK) {
        return BOOT_CONFIRM_ERR_SLOT;
    }

    metadata_status =
        boot_metadata_recover_from_flash(flash, &metadata, &recovery);
    if (metadata_status == BOOT_METADATA_ERR_AMBIGUOUS) {
        return BOOT_CONFIRM_ERR_AMBIGUOUS_METADATA;
    }
    if (metadata_status != BOOT_METADATA_OK) {
        return BOOT_CONFIRM_ERR_METADATA;
    }

    result->metadata_before = metadata;
    result->metadata_recovery = recovery;

    if ((metadata.state == BOOT_METADATA_STATE_CONFIRMED) &&
        (metadata.active_slot == running_slot) &&
        (metadata.candidate_slot == BOOT_SLOT_NONE) &&
        (metadata.confirmation_state == 1UL)) {
        result->already_confirmed = 1U;
        result->confirmed_slot = running_slot;
        result->image_version = metadata.candidate_image_version;
        return BOOT_CONFIRM_OK;
    }

    if ((metadata.state != BOOT_METADATA_STATE_PENDING_TRIAL) ||
        (metadata.candidate_slot != running_slot) ||
        (metadata.confirmation_state != 0UL)) {
        return BOOT_CONFIRM_ERR_NOT_PENDING;
    }

    metadata_status = boot_metadata_prepare_next(
        &metadata,
        BOOT_METADATA_STATE_CONFIRMED,
        running_slot,
        BOOT_SLOT_NONE,
        metadata.candidate_image_version,
        0U,
        1U,
        0U,
        &confirmed
    );
    if (metadata_status != BOOT_METADATA_OK) {
        return BOOT_CONFIRM_ERR_NOT_PENDING;
    }

    if (boot_metadata_commit(flash, &confirmed) != BOOT_METADATA_OK) {
        return BOOT_CONFIRM_ERR_COMMIT;
    }

    result->confirmed_slot = running_slot;
    result->image_version = metadata.candidate_image_version;
    result->already_confirmed = 0U;
    return BOOT_CONFIRM_OK;
}

const char *boot_confirm_status_text(boot_confirm_status_t status)
{
    switch (status) {
    case BOOT_CONFIRM_OK:                     return "OK";
    case BOOT_CONFIRM_ERR_INVALID_ARGUMENT:   return "INVALID ARGUMENT";
    case BOOT_CONFIRM_ERR_METADATA:           return "METADATA";
    case BOOT_CONFIRM_ERR_AMBIGUOUS_METADATA: return "AMBIGUOUS METADATA";
    case BOOT_CONFIRM_ERR_SLOT:               return "SLOT";
    case BOOT_CONFIRM_ERR_NOT_PENDING:        return "NOT PENDING";
    case BOOT_CONFIRM_ERR_COMMIT:             return "COMMIT";
    default:                                  return "UNKNOWN";
    }
}
