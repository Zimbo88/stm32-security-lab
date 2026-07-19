#ifndef SIGNED_IMAGE_H
#define SIGNED_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "boot_slot.h"
#include "flash_layout.h"
#include "image_policy.h"

#define SIGNED_PAYLOAD_HASH_SIZE 64U

typedef struct {
    uint32_t magic;
    uint32_t header_version;
    uint32_t image_version;
    uint32_t vector_address;
    uint32_t image_size;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t payload_sha512[SIGNED_PAYLOAD_HASH_SIZE];
} signed_manifest_t;

typedef enum {
    VERIFY_OK = 0,
    VERIFY_BAD_MAGIC,
    VERIFY_BAD_HEADER_VERSION,
    VERIFY_ROLLBACK_VERSION,
    VERIFY_BAD_VECTOR_ADDRESS,
    VERIFY_BAD_SIZE,
    VERIFY_BAD_STACK,
    VERIFY_BAD_RESET_VECTOR,
    VERIFY_BAD_PAYLOAD_HASH,
    VERIFY_BAD_SIGNATURE,
    VERIFY_BAD_FLAGS,
    VERIFY_BAD_RESERVED,
    VERIFY_BAD_PAYLOAD_RANGE,
    VERIFY_BAD_TARGET_COMPATIBILITY,
    VERIFY_BAD_IMAGE_TYPE
} verify_status_t;

typedef struct {
    uint32_t vector_address;
    uint32_t image_size;
    uint32_t payload_end;
    uint32_t initial_msp;
    uint32_t reset_vector;
    uint32_t reset_address;
} signed_image_jump_context_t;

verify_status_t signed_image_verify_buffer(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    const uint8_t signature_bytes[SIGNED_SIGNATURE_SIZE],
    const uint8_t *payload,
    size_t payload_capacity,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE]
);
verify_status_t signed_image_decode_manifest(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    signed_manifest_t *manifest
);
verify_status_t signed_image_verify_update_slot_buffer(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    const uint8_t signature_bytes[SIGNED_SIGNATURE_SIZE],
    const uint8_t *payload,
    size_t payload_capacity,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const boot_slot_descriptor_t *slot
);
verify_status_t signed_image_verify(void);
verify_status_t signed_image_prepare_jump(signed_image_jump_context_t *context);
const char *signed_image_status_text(verify_status_t status);
verify_status_t signed_image_jump(const signed_image_jump_context_t *context);

#endif
