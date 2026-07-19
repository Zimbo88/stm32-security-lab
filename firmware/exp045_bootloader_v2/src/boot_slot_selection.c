#include "boot_slot_selection.h"

#include <string.h>

_Static_assert(
    BOOT_METADATA_MAX_BOOT_ATTEMPTS > 0UL,
    "trial boot policy requires at least one configured attempt"
);

static void init_result(boot_slot_selection_result_t *result)
{
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->decision = BOOT_SLOT_SELECTION_DECISION_NONE;
    result->selected_slot = BOOT_SLOT_NONE;
    result->confirmed_slot = BOOT_SLOT_NONE;
    result->candidate_slot = BOOT_SLOT_NONE;
    result->selected_verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    result->candidate_verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    result->confirmed_verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    result->fallback_cause = BOOT_SLOT_SELECTION_OK;
}

static boot_slot_selection_status_t maybe_inject(
    const boot_slot_selection_options_t *options,
    boot_slot_selection_fault_point_t point,
    uint32_t detail
)
{
    if ((options != NULL) && (options->fault_hook != NULL)) {
        const boot_slot_selection_status_t status =
            options->fault_hook(options->fault_context, point, detail);
        if (status != BOOT_SLOT_SELECTION_OK) {
            return status;
        }
    }

    return BOOT_SLOT_SELECTION_OK;
}

static boot_slot_selection_status_t verify_slot(
    const boot_slot_selection_options_t *options,
    const boot_slot_descriptor_t *slot,
    signed_image_jump_context_t *jump_context,
    verify_status_t *verify_status,
    boot_slot_selection_fault_point_t fault_point
)
{
    boot_slot_selection_status_t status =
        maybe_inject(options, fault_point, (uint32_t)slot->id);

    if (status != BOOT_SLOT_SELECTION_OK) {
        return status;
    }

    *verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    status = options->verify_slot(
        options->verify_context,
        slot,
        jump_context,
        verify_status
    );
    if (status != BOOT_SLOT_SELECTION_OK) {
        return status;
    }

    return (*verify_status == VERIFY_OK)
        ? BOOT_SLOT_SELECTION_OK
        : BOOT_SLOT_SELECTION_ERR_VERIFY;
}

static boot_slot_selection_status_t commit_next(
    const boot_slot_selection_options_t *options,
    const boot_metadata_record_t *next,
    boot_slot_selection_fault_point_t fault_point,
    uint32_t detail
)
{
    boot_slot_selection_status_t injected =
        maybe_inject(options, fault_point, detail);

    if (injected != BOOT_SLOT_SELECTION_OK) {
        return injected;
    }

    return (boot_metadata_commit(options->metadata_flash, next) ==
            BOOT_METADATA_OK)
        ? BOOT_SLOT_SELECTION_OK
        : BOOT_SLOT_SELECTION_ERR_COMMIT;
}

static void prepare_rejected_invalid(
    const boot_metadata_record_t *current,
    uint32_t result_code,
    boot_metadata_record_t *next
)
{
    if (boot_metadata_prepare_next(
            current,
            BOOT_METADATA_STATE_REJECTED_INVALID,
            current->active_slot,
            current->candidate_slot,
            current->candidate_image_version,
            0U,
            0U,
            result_code,
            next
        ) != BOOT_METADATA_OK) {
        (void)boot_metadata_empty(next);
    }
}

static void reject_invalid_best_effort(
    const boot_slot_selection_options_t *options,
    const boot_metadata_record_t *current,
    uint32_t result_code
)
{
    boot_metadata_record_t rejected;

    prepare_rejected_invalid(current, result_code, &rejected);
    if (rejected.sequence != 0UL) {
        (void)commit_next(
            options,
            &rejected,
            BOOT_SLOT_SELECTION_FAULT_METADATA_REJECT_INVALID,
            result_code
        );
    }
}

static boot_slot_selection_status_t select_confirmed_slot(
    const boot_slot_selection_options_t *options,
    const boot_metadata_record_t *metadata,
    boot_slot_selection_decision_t decision,
    boot_slot_selection_result_t *result
)
{
    const boot_slot_descriptor_t *slot = NULL;
    signed_image_jump_context_t jump_context;
    verify_status_t verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    boot_slot_selection_status_t status;

    if (boot_slot_lookup(metadata->active_slot, &slot) !=
        BOOT_SLOT_LOOKUP_OK) {
        return BOOT_SLOT_SELECTION_ERR_SLOT;
    }

    status = verify_slot(
        options,
        slot,
        &jump_context,
        &verify_status,
        BOOT_SLOT_SELECTION_FAULT_VERIFY_CONFIRMED
    );

    result->confirmed_verify_status = verify_status;

    if (status != BOOT_SLOT_SELECTION_OK) {
        return (status == BOOT_SLOT_SELECTION_ERR_VERIFY)
            ? BOOT_SLOT_SELECTION_ERR_NO_BOOTABLE_SLOT
            : status;
    }

    result->decision = decision;
    result->selected_slot = (uint32_t)slot->id;
    result->selected_verify_status = verify_status;
    result->jump_context = jump_context;
    return BOOT_SLOT_SELECTION_OK;
}

static boot_slot_selection_status_t fallback_to_confirmed(
    const boot_slot_selection_options_t *options,
    const boot_metadata_record_t *metadata,
    boot_slot_selection_result_t *result
)
{
    if (metadata->active_slot == BOOT_SLOT_NONE) {
        return BOOT_SLOT_SELECTION_ERR_NO_BOOTABLE_SLOT;
    }

    return select_confirmed_slot(
        options,
        metadata,
        BOOT_SLOT_SELECTION_DECISION_FALLBACK,
        result
    );
}

static boot_slot_selection_status_t start_or_continue_trial(
    const boot_slot_selection_options_t *options,
    const boot_metadata_record_t *metadata,
    boot_slot_selection_result_t *result
)
{
    const boot_slot_descriptor_t *slot = NULL;
    signed_image_jump_context_t jump_context;
    verify_status_t verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    boot_metadata_record_t next;
    uint32_t next_attempts = 0U;
    boot_slot_selection_fault_point_t fault_point =
        BOOT_SLOT_SELECTION_FAULT_METADATA_PENDING_TRIAL;
    boot_slot_selection_status_t status;

    if (boot_slot_lookup(metadata->candidate_slot, &slot) !=
        BOOT_SLOT_LOOKUP_OK) {
        result->fallback_cause = BOOT_SLOT_SELECTION_ERR_SLOT;
        return fallback_to_confirmed(options, metadata, result);
    }

    status = verify_slot(
        options,
        slot,
        &jump_context,
        &verify_status,
        BOOT_SLOT_SELECTION_FAULT_VERIFY_CANDIDATE
    );
    result->candidate_verify_status = verify_status;

    if (status != BOOT_SLOT_SELECTION_OK) {
        result->fallback_cause = status;
        reject_invalid_best_effort(
            options,
            metadata,
            BOOT_SLOT_SELECTION_RESULT_CANDIDATE_VERIFY_FAILED
        );
        return fallback_to_confirmed(options, metadata, result);
    }

    if (metadata->state == BOOT_METADATA_STATE_CANDIDATE_READY) {
        next_attempts = BOOT_METADATA_MAX_BOOT_ATTEMPTS - 1UL;
    } else if (metadata->boot_attempt_count > 0UL) {
        next_attempts = metadata->boot_attempt_count - 1UL;
        fault_point = BOOT_SLOT_SELECTION_FAULT_METADATA_ATTEMPT_DECREMENT;
    } else {
        reject_invalid_best_effort(
            options,
            metadata,
            BOOT_SLOT_SELECTION_RESULT_ATTEMPTS_EXHAUSTED
        );
        result->fallback_cause = BOOT_SLOT_SELECTION_ERR_NO_BOOTABLE_SLOT;
        return fallback_to_confirmed(options, metadata, result);
    }

    if (boot_metadata_prepare_next(
            metadata,
            BOOT_METADATA_STATE_PENDING_TRIAL,
            metadata->active_slot,
            metadata->candidate_slot,
            metadata->candidate_image_version,
            next_attempts,
            0U,
            BOOT_SLOT_SELECTION_RESULT_NONE,
            &next
        ) != BOOT_METADATA_OK) {
        result->fallback_cause = BOOT_SLOT_SELECTION_ERR_METADATA;
        return fallback_to_confirmed(options, metadata, result);
    }

    status = commit_next(options, &next, fault_point, next_attempts);
    if (status != BOOT_SLOT_SELECTION_OK) {
        result->fallback_cause = status;
        return fallback_to_confirmed(options, metadata, result);
    }

    result->decision = BOOT_SLOT_SELECTION_DECISION_TRIAL;
    result->selected_slot = (uint32_t)slot->id;
    result->selected_verify_status = verify_status;
    result->attempts_remaining_after = next_attempts;
    result->jump_context = jump_context;
    return BOOT_SLOT_SELECTION_OK;
}

boot_slot_selection_status_t boot_slot_selection_select(
    const boot_slot_selection_options_t *options,
    boot_slot_selection_result_t *result
)
{
    boot_metadata_record_t metadata;
    boot_metadata_recovery_t recovery;
    boot_metadata_status_t metadata_status;

    if ((options == NULL) ||
        (options->metadata_flash == NULL) ||
        (options->verify_slot == NULL) ||
        (result == NULL)) {
        return BOOT_SLOT_SELECTION_ERR_INVALID_ARGUMENT;
    }

    init_result(result);

    metadata_status = boot_metadata_recover_from_flash(
        options->metadata_flash,
        &metadata,
        &recovery
    );
    if (metadata_status == BOOT_METADATA_ERR_AMBIGUOUS) {
        return BOOT_SLOT_SELECTION_ERR_AMBIGUOUS_METADATA;
    }
    if (metadata_status != BOOT_METADATA_OK) {
        return BOOT_SLOT_SELECTION_ERR_METADATA;
    }

    result->metadata_before = metadata;
    result->metadata_recovery = recovery;
    result->confirmed_slot = metadata.active_slot;
    result->candidate_slot = metadata.candidate_slot;
    result->image_version = metadata.candidate_image_version;
    result->attempts_remaining_before = metadata.boot_attempt_count;
    result->attempts_remaining_after = metadata.boot_attempt_count;

    switch (metadata.state) {
    case BOOT_METADATA_STATE_CONFIRMED:
        return select_confirmed_slot(
            options,
            &metadata,
            BOOT_SLOT_SELECTION_DECISION_CONFIRMED,
            result
        );

    case BOOT_METADATA_STATE_CANDIDATE_READY:
        return start_or_continue_trial(options, &metadata, result);

    case BOOT_METADATA_STATE_PENDING_TRIAL:
        return start_or_continue_trial(options, &metadata, result);

    case BOOT_METADATA_STATE_WRITING:
    case BOOT_METADATA_STATE_REJECTED_INVALID:
        return fallback_to_confirmed(options, &metadata, result);

    case BOOT_METADATA_STATE_EMPTY:
    default:
        return BOOT_SLOT_SELECTION_ERR_NO_BOOTABLE_SLOT;
    }
}

const char *boot_slot_selection_status_text(boot_slot_selection_status_t status)
{
    switch (status) {
    case BOOT_SLOT_SELECTION_OK:                     return "OK";
    case BOOT_SLOT_SELECTION_ERR_INVALID_ARGUMENT:   return "INVALID ARGUMENT";
    case BOOT_SLOT_SELECTION_ERR_METADATA:           return "METADATA";
    case BOOT_SLOT_SELECTION_ERR_AMBIGUOUS_METADATA: return "AMBIGUOUS METADATA";
    case BOOT_SLOT_SELECTION_ERR_SLOT:               return "SLOT";
    case BOOT_SLOT_SELECTION_ERR_VERIFY:             return "VERIFY";
    case BOOT_SLOT_SELECTION_ERR_COMMIT:             return "COMMIT";
    case BOOT_SLOT_SELECTION_ERR_NO_BOOTABLE_SLOT:   return "NO BOOTABLE SLOT";
    case BOOT_SLOT_SELECTION_ERR_INJECTED:           return "INJECTED";
    default:                                         return "UNKNOWN";
    }
}

const char *boot_slot_selection_decision_text(boot_slot_selection_decision_t decision)
{
    switch (decision) {
    case BOOT_SLOT_SELECTION_DECISION_NONE:      return "NONE";
    case BOOT_SLOT_SELECTION_DECISION_CONFIRMED: return "CONFIRMED";
    case BOOT_SLOT_SELECTION_DECISION_TRIAL:     return "TRIAL";
    case BOOT_SLOT_SELECTION_DECISION_FALLBACK:  return "FALLBACK";
    default:                                     return "UNKNOWN";
    }
}
