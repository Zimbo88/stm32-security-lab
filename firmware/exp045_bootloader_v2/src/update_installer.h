#ifndef UPDATE_INSTALLER_H
#define UPDATE_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "boot_metadata.h"
#include "boot_slot.h"
#include "signed_image.h"
#include "update_package.h"

typedef enum {
    UPDATE_INSTALL_OK = 0,
    UPDATE_INSTALL_ERR_INVALID_ARGUMENT = 1,
    UPDATE_INSTALL_ERR_PACKAGE = 2,
    UPDATE_INSTALL_ERR_METADATA = 3,
    UPDATE_INSTALL_ERR_ACTIVE_STATE = 4,
    UPDATE_INSTALL_ERR_SLOT = 5,
    UPDATE_INSTALL_ERR_ROLLBACK = 6,
    UPDATE_INSTALL_ERR_CAPACITY = 7,
    UPDATE_INSTALL_ERR_ERASE = 8,
    UPDATE_INSTALL_ERR_PROGRAM = 9,
    UPDATE_INSTALL_ERR_READBACK = 10,
    UPDATE_INSTALL_ERR_HASH = 11,
    UPDATE_INSTALL_ERR_INSTALLED_VERIFY = 12,
    UPDATE_INSTALL_ERR_INJECTED = 13
} update_install_status_t;

typedef enum {
    UPDATE_INSTALL_FAULT_METADATA_WRITING = 0,
    UPDATE_INSTALL_FAULT_ERASE_SECTOR = 1,
    UPDATE_INSTALL_FAULT_PROGRAM_BLOCK = 2,
    UPDATE_INSTALL_FAULT_READBACK = 3,
    UPDATE_INSTALL_FAULT_HASH_COMPLETE = 4,
    UPDATE_INSTALL_FAULT_VERIFY_INSTALLED = 5,
    UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY = 6,
    UPDATE_INSTALL_FAULT_HASH_BEGIN = 7
} update_install_fault_point_t;

typedef update_install_status_t (*update_install_fault_hook_t)(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
);

typedef struct {
    uint8_t *program_buffer;
    size_t program_buffer_size;
    uint8_t *readback_buffer;
    size_t readback_buffer_size;
    uint8_t *installed_image_buffer;
    size_t installed_image_buffer_size;
    update_install_fault_hook_t fault_hook;
    void *fault_context;
} update_install_options_t;

typedef struct {
    uint32_t active_slot;
    uint32_t candidate_slot;
    uint32_t image_version;
    uint32_t erased_sector_count;
    uint32_t programmed_block_count;
    verify_status_t package_verify_status;
    verify_status_t installed_verify_status;
    boot_metadata_recovery_t metadata_recovery;
} update_install_result_t;

update_install_status_t update_installer_install(
    const boot_flash_t *flash,
    const uint8_t *package_bytes,
    size_t package_size,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
);
const char *update_install_status_text(update_install_status_t status);

#endif
