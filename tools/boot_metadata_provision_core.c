#include "boot_metadata_provision_core.h"

static boot_metadata_status_t prepare_confirmed(
    uint32_t active_slot,
    uint32_t image_version,
    boot_metadata_record_t *record
)
{
    boot_metadata_record_t empty;
    boot_metadata_status_t status;

    if (record == NULL) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    status = boot_metadata_empty(&empty);
    if (status != BOOT_METADATA_OK) {
        return status;
    }

    return boot_metadata_prepare_next(
        &empty,
        BOOT_METADATA_STATE_CONFIRMED,
        active_slot,
        BOOT_SLOT_NONE,
        image_version,
        0U,
        1U,
        0U,
        record
    );
}

boot_metadata_status_t boot_metadata_provision_confirmed_record(
    uint32_t active_slot,
    uint32_t image_version,
    boot_metadata_record_t *record
)
{
    return prepare_confirmed(active_slot, image_version, record);
}

boot_metadata_status_t boot_metadata_provision_confirmed_image(
    uint32_t active_slot,
    uint32_t image_version,
    uint8_t output[STM32F429_BOOT_METADATA_RECORD_SIZE]
)
{
    boot_metadata_record_t record;
    boot_metadata_status_t status;

    if (output == NULL) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }

    status = prepare_confirmed(active_slot, image_version, &record);
    if (status != BOOT_METADATA_OK) {
        return status;
    }

    return boot_metadata_encode(&record, output);
}

boot_metadata_status_t boot_metadata_provision_update_sequence(
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t active_image_version,
    uint32_t candidate_image_version,
    uint8_t confirmed[STM32F429_BOOT_METADATA_RECORD_SIZE],
    uint8_t writing[STM32F429_BOOT_METADATA_RECORD_SIZE],
    uint8_t candidate_ready[STM32F429_BOOT_METADATA_RECORD_SIZE]
)
{
    boot_metadata_record_t confirmed_record;
    boot_metadata_record_t writing_record;
    boot_metadata_record_t ready_record;
    boot_metadata_status_t status;

    if ((confirmed == NULL) || (writing == NULL) || (candidate_ready == NULL)) {
        return BOOT_METADATA_ERR_INVALID_ARGUMENT;
    }
    if ((active_slot == candidate_slot) ||
        (candidate_image_version <= active_image_version)) {
        return BOOT_METADATA_ERR_BAD_FORMAT;
    }

    status = prepare_confirmed(active_slot, active_image_version, &confirmed_record);
    if (status != BOOT_METADATA_OK) {
        return status;
    }

    status = boot_metadata_prepare_next(
        &confirmed_record,
        BOOT_METADATA_STATE_WRITING,
        active_slot,
        candidate_slot,
        candidate_image_version,
        0U,
        0U,
        0U,
        &writing_record
    );
    if (status != BOOT_METADATA_OK) {
        return status;
    }

    status = boot_metadata_prepare_next(
        &writing_record,
        BOOT_METADATA_STATE_CANDIDATE_READY,
        active_slot,
        candidate_slot,
        candidate_image_version,
        0U,
        0U,
        0U,
        &ready_record
    );
    if (status != BOOT_METADATA_OK) {
        return status;
    }

    status = boot_metadata_encode(&confirmed_record, confirmed);
    if (status != BOOT_METADATA_OK) {
        return status;
    }
    status = boot_metadata_encode(&writing_record, writing);
    if (status != BOOT_METADATA_OK) {
        return status;
    }
    return boot_metadata_encode(&ready_record, candidate_ready);
}
