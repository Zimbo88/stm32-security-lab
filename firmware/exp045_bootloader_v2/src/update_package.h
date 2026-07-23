#ifndef UPDATE_PACKAGE_H
#define UPDATE_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "boot_slot.h"
#include "signed_image.h"

typedef enum {
    UPDATE_PACKAGE_OK = 0,
    UPDATE_PACKAGE_ERR_INVALID_ARGUMENT = 1,
    UPDATE_PACKAGE_ERR_TRUNCATED = 2,
    UPDATE_PACKAGE_ERR_TRAILING_DATA = 3,
    UPDATE_PACKAGE_ERR_OVERSIZED = 4,
    UPDATE_PACKAGE_ERR_UNSUPPORTED_VERSION = 5,
    UPDATE_PACKAGE_ERR_UNKNOWN_FLAGS = 6,
    UPDATE_PACKAGE_ERR_BAD_TARGET = 7,
    UPDATE_PACKAGE_ERR_BAD_IMAGE_TYPE = 8,
    UPDATE_PACKAGE_ERR_BAD_SIZE = 9,
    UPDATE_PACKAGE_ERR_BAD_PADDING = 10,
    UPDATE_PACKAGE_ERR_INCOMPATIBLE_SLOT = 11,
    UPDATE_PACKAGE_ERR_VERIFY = 12
} update_package_status_t;

typedef struct {
    const uint8_t *package_bytes;
    size_t package_size;
    const uint8_t *manifest_bytes;
    const uint8_t *signature_bytes;
    const uint8_t *payload;
    size_t payload_size;
    signed_manifest_t manifest;
} update_package_t;

typedef struct {
    signed_manifest_t manifest;
    uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE];
    uint8_t signature_bytes[SIGNED_SIGNATURE_SIZE];
    size_t payload_size;
    size_t package_size;
} update_package_header_t;

update_package_status_t update_package_parse(
    const uint8_t *package_bytes,
    size_t package_size,
    update_package_t *package
);
update_package_status_t update_package_verify_header_for_slot(
    const uint8_t *header_bytes,
    size_t header_size,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const boot_slot_descriptor_t *slot,
    update_package_header_t *header,
    verify_status_t *verify_status
);
update_package_status_t update_package_verify_for_slot(
    const uint8_t *package_bytes,
    size_t package_size,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const boot_slot_descriptor_t *slot,
    update_package_t *package,
    verify_status_t *verify_status
);
const char *update_package_status_text(update_package_status_t status);

#endif
