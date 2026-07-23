#ifndef UPDATE_INSTALLER_H
#define UPDATE_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "boot_metadata.h"
#include "boot_slot.h"
#include "monocypher-ed25519.h"
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
    UPDATE_INSTALL_ERR_INJECTED = 13,
    UPDATE_INSTALL_ERR_STATE = 14,
    UPDATE_INSTALL_ERR_SEQUENCE = 15
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

typedef enum {
    UPDATE_INSTALL_SESSION_EMPTY = 0,          /* session storage is unused */
    UPDATE_INSTALL_SESSION_INITIALIZED,        /* slot and metadata are selected */
    UPDATE_INSTALL_SESSION_WRITING,            /* WRITING metadata is committed */
    UPDATE_INSTALL_SESSION_PAYLOAD_COMPLETE,   /* all payload bytes were accepted */
    UPDATE_INSTALL_SESSION_FINISHED,           /* CANDIDATE_READY was committed */
    UPDATE_INSTALL_SESSION_ABORTED,            /* install was explicitly aborted */
    UPDATE_INSTALL_SESSION_FAILED              /* session cannot safely continue */
} update_installer_session_state_t;

typedef struct {
    update_installer_session_state_t state;

    boot_flash_t restricted_flash;
    boot_flash_region_t write_regions[3];

    const boot_flash_t *flash;
    const uint8_t *public_key;
    update_install_options_t options;
    update_install_result_t *result;

    boot_metadata_record_t metadata_before;
    boot_metadata_record_t writing_metadata;
    boot_metadata_recovery_t metadata_recovery;

    const boot_slot_descriptor_t *active;
    const boot_slot_descriptor_t *candidate;

    update_package_header_t header;
    crypto_sha512_ctx payload_hash_ctx;

    size_t payload_received;
    size_t payload_programmed;
    size_t payload_program_size;
    size_t program_fill;
    uint32_t programmed_block_count;

    uint8_t metadata_writing_committed;
    uint8_t candidate_erased;
    uint8_t payload_hash_finalized;
} update_installer_session_t;

update_install_status_t update_installer_session_init(
    update_installer_session_t *session,
    const boot_flash_t *flash,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
);
update_install_status_t update_installer_begin(
    update_installer_session_t *session,
    const uint8_t *header_bytes,
    size_t header_size
);
update_install_status_t update_installer_write(
    update_installer_session_t *session,
    size_t payload_offset,
    const uint8_t *data,
    size_t length
);
update_install_status_t update_installer_finish(update_installer_session_t *session);
update_install_status_t update_installer_abort(update_installer_session_t *session);
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
