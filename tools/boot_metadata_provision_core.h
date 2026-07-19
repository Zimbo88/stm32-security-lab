#ifndef BOOT_METADATA_PROVISION_CORE_H
#define BOOT_METADATA_PROVISION_CORE_H

#include <stdint.h>

#include "boot_metadata.h"

boot_metadata_status_t boot_metadata_provision_confirmed_record(
    uint32_t active_slot,
    uint32_t image_version,
    boot_metadata_record_t *record
);
boot_metadata_status_t boot_metadata_provision_confirmed_image(
    uint32_t active_slot,
    uint32_t image_version,
    uint8_t output[STM32F429_BOOT_METADATA_RECORD_SIZE]
);
boot_metadata_status_t boot_metadata_provision_update_sequence(
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t active_image_version,
    uint32_t candidate_image_version,
    uint8_t confirmed[STM32F429_BOOT_METADATA_RECORD_SIZE],
    uint8_t writing[STM32F429_BOOT_METADATA_RECORD_SIZE],
    uint8_t candidate_ready[STM32F429_BOOT_METADATA_RECORD_SIZE]
);

#endif
