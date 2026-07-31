#include "platform_confirmation.h"

#include <string.h>

#include "boot_flash_target.h"
#include "boot_slot.h"
#include "mpu_policy.h"

static platform_confirmation_snapshot_t last_snapshot = {
    .running_slot = BOOT_SLOT_NONE,
    .metadata_state = BOOT_METADATA_STATE_EMPTY,
    .metadata_sequence = 0UL,
    .confirmed_slot = BOOT_SLOT_NONE,
    .candidate_slot = BOOT_SLOT_NONE,
    .remaining_trial_attempts = 0UL,
    .image_version = 0UL,
    .metadata_allows_confirmation = 0U,
    .already_confirmed = 0U,
    .last_status = PLATFORM_CONFIRMATION_NOT_HEALTHY,
    .confirm_status = BOOT_CONFIRM_ERR_NOT_PENDING,
    .metadata_status = BOOT_METADATA_ERR_NO_VALID_COPY,
};

static void snapshot_clear(void)
{
    last_snapshot.running_slot = BOOT_SLOT_NONE;
    last_snapshot.metadata_state = BOOT_METADATA_STATE_EMPTY;
    last_snapshot.metadata_sequence = 0UL;
    last_snapshot.confirmed_slot = BOOT_SLOT_NONE;
    last_snapshot.candidate_slot = BOOT_SLOT_NONE;
    last_snapshot.remaining_trial_attempts = 0UL;
    last_snapshot.image_version = 0UL;
    last_snapshot.metadata_allows_confirmation = 0U;
    last_snapshot.already_confirmed = 0U;
    last_snapshot.confirm_status = BOOT_CONFIRM_ERR_NOT_PENDING;
    last_snapshot.metadata_status = BOOT_METADATA_ERR_NO_VALID_COPY;
}

uint8_t platform_confirmation_health_gate(
    const platform_confirmation_health_t *health
)
{
    if (health == NULL) {
        return 0U;
    }

    return ((health->early_platform_init_done != 0U) &&
            (health->running_slot_identified != 0U) &&
            (health->core_self_checks_passed != 0U) &&
            (health->critical_initialization_failure == 0U) &&
            (health->stable_execution_point_reached != 0U) &&
            (health->uart_diagnostics_ready != 0U) &&
            (health->runtime_monitor_ready != 0U) &&
            (health->mpu_policy_ready != 0U) &&
            (health->watchdog_active != 0U) &&
            (health->application_health_ok != 0U) &&
            (health->metadata_allows_confirmation != 0U))
        ? 1U
        : 0U;
}

static uint8_t metadata_allows_confirmation(
    const boot_metadata_record_t *metadata,
    uint32_t running_slot,
    uint8_t *already_confirmed
)
{
    if (already_confirmed != NULL) {
        *already_confirmed = 0U;
    }

    if (metadata == NULL) {
        return 0U;
    }

    if ((metadata->state == BOOT_METADATA_STATE_PENDING_TRIAL) &&
        (metadata->candidate_slot == running_slot) &&
        (metadata->confirmation_state == 0UL)) {
        return 1U;
    }

    if ((metadata->state == BOOT_METADATA_STATE_CONFIRMED) &&
        (metadata->active_slot == running_slot) &&
        (metadata->candidate_slot == BOOT_SLOT_NONE) &&
        (metadata->confirmation_state == 1UL)) {
        if (already_confirmed != NULL) {
            *already_confirmed = 1U;
        }
        return 1U;
    }

    return 0U;
}

static platform_confirmation_status_t map_confirm_status(
    boot_confirm_status_t status
)
{
    switch (status) {
    case BOOT_CONFIRM_OK:
        return PLATFORM_CONFIRMATION_OK;
    case BOOT_CONFIRM_ERR_INVALID_ARGUMENT:
    case BOOT_CONFIRM_ERR_SLOT:
        return PLATFORM_CONFIRMATION_SLOT;
    case BOOT_CONFIRM_ERR_METADATA:
        return PLATFORM_CONFIRMATION_METADATA;
    case BOOT_CONFIRM_ERR_AMBIGUOUS_METADATA:
        return PLATFORM_CONFIRMATION_AMBIGUOUS_METADATA;
    case BOOT_CONFIRM_ERR_NOT_PENDING:
        return PLATFORM_CONFIRMATION_NOT_PENDING;
    case BOOT_CONFIRM_ERR_COMMIT:
        return PLATFORM_CONFIRMATION_COMMIT;
    default:
        return PLATFORM_CONFIRMATION_METADATA;
    }
}

platform_confirmation_status_t platform_confirmation_service(
    uint32_t vector_address,
    const platform_confirmation_health_t *base_health
)
{
    boot_flash_t flash;
    boot_metadata_record_t metadata;
    boot_metadata_recovery_t recovery;
    boot_confirm_result_t result;
    platform_confirmation_health_t health;
    uint32_t running_slot = BOOT_SLOT_NONE;
    uint8_t already_confirmed = 0U;

    snapshot_clear();

    if (base_health == NULL) {
        last_snapshot.last_status = PLATFORM_CONFIRMATION_NOT_HEALTHY;
        return last_snapshot.last_status;
    }
    health = *base_health;

    last_snapshot.confirm_status = boot_confirm_slot_from_vector_address(
        vector_address,
        &running_slot
    );
    if (last_snapshot.confirm_status != BOOT_CONFIRM_OK) {
        last_snapshot.last_status = PLATFORM_CONFIRMATION_SLOT;
        return last_snapshot.last_status;
    }
    last_snapshot.running_slot = running_slot;
    health.running_slot_identified = 1U;

    if (boot_flash_target_init_metadata(&flash) != BOOT_FLASH_OK) {
        last_snapshot.last_status = PLATFORM_CONFIRMATION_FLASH;
        return last_snapshot.last_status;
    }

    last_snapshot.metadata_status =
        boot_metadata_recover_from_flash(&flash, &metadata, &recovery);
    if (last_snapshot.metadata_status == BOOT_METADATA_ERR_AMBIGUOUS) {
        last_snapshot.last_status = PLATFORM_CONFIRMATION_AMBIGUOUS_METADATA;
        return last_snapshot.last_status;
    }
    if (last_snapshot.metadata_status != BOOT_METADATA_OK) {
        last_snapshot.last_status = PLATFORM_CONFIRMATION_METADATA;
        return last_snapshot.last_status;
    }

    last_snapshot.metadata_state = (uint32_t)metadata.state;
    last_snapshot.metadata_sequence = metadata.sequence;
    last_snapshot.confirmed_slot = metadata.active_slot;
    last_snapshot.candidate_slot = metadata.candidate_slot;
    last_snapshot.remaining_trial_attempts = metadata.boot_attempt_count;
    last_snapshot.image_version = metadata.candidate_image_version;
    health.metadata_allows_confirmation = metadata_allows_confirmation(
        &metadata,
        running_slot,
        &already_confirmed
    );
    last_snapshot.metadata_allows_confirmation =
        health.metadata_allows_confirmation;
    last_snapshot.already_confirmed = already_confirmed;

    if (platform_confirmation_health_gate(&health) == 0U) {
        if ((health.early_platform_init_done != 0U) &&
            (health.running_slot_identified != 0U) &&
            (health.core_self_checks_passed != 0U) &&
            (health.critical_initialization_failure == 0U) &&
            (health.stable_execution_point_reached != 0U) &&
            (health.metadata_allows_confirmation == 0U)) {
            last_snapshot.last_status = PLATFORM_CONFIRMATION_NOT_PENDING;
        } else {
            last_snapshot.last_status = PLATFORM_CONFIRMATION_NOT_HEALTHY;
        }
        return last_snapshot.last_status;
    }

    /* The MPU makes the metadata journal read-only to application code. The
       existing confirmation primitive is the sole bounded exception; it is
       resumed immediately after the atomic metadata commit attempt. */
    mpu_policy_suspend_for_metadata_commit();
    last_snapshot.confirm_status =
        boot_confirm_current_slot(&flash, running_slot, &result);
    mpu_policy_resume_after_metadata_commit();
    last_snapshot.last_status =
        map_confirm_status(last_snapshot.confirm_status);
    if (last_snapshot.confirm_status == BOOT_CONFIRM_OK) {
        last_snapshot.confirmed_slot = result.confirmed_slot;
        last_snapshot.image_version = result.image_version;
        last_snapshot.already_confirmed = result.already_confirmed;
    }

    return last_snapshot.last_status;
}

void platform_confirmation_snapshot(platform_confirmation_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memcpy(snapshot, &last_snapshot, sizeof(*snapshot));
}

const char *platform_confirmation_status_text(
    platform_confirmation_status_t status
)
{
    switch (status) {
    case PLATFORM_CONFIRMATION_OK:                 return "OK";
    case PLATFORM_CONFIRMATION_NOT_HEALTHY:        return "NOT HEALTHY";
    case PLATFORM_CONFIRMATION_SLOT:               return "SLOT";
    case PLATFORM_CONFIRMATION_FLASH:              return "FLASH";
    case PLATFORM_CONFIRMATION_METADATA:           return "METADATA";
    case PLATFORM_CONFIRMATION_AMBIGUOUS_METADATA: return "AMBIGUOUS METADATA";
    case PLATFORM_CONFIRMATION_NOT_PENDING:        return "NOT PENDING";
    case PLATFORM_CONFIRMATION_COMMIT:             return "COMMIT";
    default:                                       return "UNKNOWN";
    }
}
