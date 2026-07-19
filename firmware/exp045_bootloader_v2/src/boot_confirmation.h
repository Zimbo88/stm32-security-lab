#ifndef BOOT_CONFIRMATION_H
#define BOOT_CONFIRMATION_H

#include <stdint.h>

#include "boot_flash.h"
#include "boot_metadata.h"
#include "boot_slot.h"

typedef enum {
    BOOT_CONFIRM_OK = 0,
    BOOT_CONFIRM_ERR_INVALID_ARGUMENT = 1,
    BOOT_CONFIRM_ERR_METADATA = 2,
    BOOT_CONFIRM_ERR_AMBIGUOUS_METADATA = 3,
    BOOT_CONFIRM_ERR_SLOT = 4,
    BOOT_CONFIRM_ERR_NOT_PENDING = 5,
    BOOT_CONFIRM_ERR_COMMIT = 6
} boot_confirm_status_t;

typedef struct {
    uint32_t confirmed_slot;
    uint32_t image_version;
    uint8_t already_confirmed;
    boot_metadata_record_t metadata_before;
    boot_metadata_recovery_t metadata_recovery;
} boot_confirm_result_t;

boot_confirm_status_t boot_confirm_current_slot(
    const boot_flash_t *flash,
    uint32_t running_slot,
    boot_confirm_result_t *result
);
boot_confirm_status_t boot_confirm_slot_from_vector_address(
    uint32_t vector_address,
    uint32_t *slot_id
);
const char *boot_confirm_status_text(boot_confirm_status_t status);

#endif
