#include "update_package.h"

#include <stdint.h>

#include "flash_layout.h"
#include "image_policy.h"
#include "stm32f429_memory_layout.h"

static uint8_t checked_size_add(size_t left, size_t right, size_t *out)
{
    if ((out == NULL) || (right > (SIZE_MAX - left))) {
        return 0U;
    }

    *out = left + right;
    return 1U;
}

static uint32_t largest_slot_payload_size(void)
{
    return (STM32F429_SLOT_A_PAYLOAD_MAX_SIZE > STM32F429_SLOT_B_PAYLOAD_MAX_SIZE)
        ? STM32F429_SLOT_A_PAYLOAD_MAX_SIZE
        : STM32F429_SLOT_B_PAYLOAD_MAX_SIZE;
}

static uint8_t padding_is_canonical(const uint8_t *package_bytes)
{
    const size_t padding_start =
        (size_t)SIGNED_MANIFEST_SIZE + (size_t)SIGNED_SIGNATURE_SIZE;

    for (size_t i = padding_start; i < (size_t)SIGNED_IMAGE_HEADER_SIZE; ++i) {
        if (package_bytes[i] != 0xFFU) {
            return 0U;
        }
    }

    return 1U;
}

update_package_status_t update_package_parse(
    const uint8_t *package_bytes,
    size_t package_size,
    update_package_t *package
)
{
    signed_manifest_t manifest;
    size_t expected_size = 0U;

    if ((package_bytes == NULL) || (package == NULL)) {
        return UPDATE_PACKAGE_ERR_INVALID_ARGUMENT;
    }

    package->package_bytes = NULL;
    package->package_size = 0U;
    package->manifest_bytes = NULL;
    package->signature_bytes = NULL;
    package->payload = NULL;
    package->payload_size = 0U;

    if (package_size < (size_t)SIGNED_IMAGE_HEADER_SIZE) {
        return UPDATE_PACKAGE_ERR_TRUNCATED;
    }

    if (signed_image_decode_manifest(package_bytes, &manifest) != VERIFY_OK) {
        return UPDATE_PACKAGE_ERR_INVALID_ARGUMENT;
    }

    if (manifest.magic != SIGNED_IMAGE_MAGIC) {
        return UPDATE_PACKAGE_ERR_VERIFY;
    }

    if (manifest.header_version != UPDATE_PACKAGE_FORMAT_VERSION) {
        return UPDATE_PACKAGE_ERR_UNSUPPORTED_VERSION;
    }

    if ((manifest.flags & ~((uint32_t)SIGNED_IMAGE_FLAGS_ALLOWED_MASK)) != 0UL) {
        return UPDATE_PACKAGE_ERR_UNKNOWN_FLAGS;
    }

    if (manifest.reserved0 != UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1) {
        return UPDATE_PACKAGE_ERR_BAD_TARGET;
    }

    if (manifest.reserved1 != UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION) {
        return UPDATE_PACKAGE_ERR_BAD_IMAGE_TYPE;
    }

    if (manifest.image_size < APPLICATION_MIN_SIZE) {
        return UPDATE_PACKAGE_ERR_BAD_SIZE;
    }

    if (manifest.image_size > largest_slot_payload_size()) {
        return UPDATE_PACKAGE_ERR_OVERSIZED;
    }

    if (checked_size_add(
            (size_t)SIGNED_IMAGE_HEADER_SIZE,
            (size_t)manifest.image_size,
            &expected_size
        ) == 0U) {
        return UPDATE_PACKAGE_ERR_OVERSIZED;
    }

    if (package_size < expected_size) {
        return UPDATE_PACKAGE_ERR_TRUNCATED;
    }

    if (package_size > expected_size) {
        return UPDATE_PACKAGE_ERR_TRAILING_DATA;
    }

    if (padding_is_canonical(package_bytes) == 0U) {
        return UPDATE_PACKAGE_ERR_BAD_PADDING;
    }

    package->package_bytes = package_bytes;
    package->package_size = package_size;
    package->manifest_bytes = package_bytes;
    package->signature_bytes = &package_bytes[SIGNED_MANIFEST_SIZE];
    package->payload = &package_bytes[SIGNED_IMAGE_HEADER_SIZE];
    package->payload_size = (size_t)manifest.image_size;
    package->manifest = manifest;
    return UPDATE_PACKAGE_OK;
}

update_package_status_t update_package_verify_for_slot(
    const uint8_t *package_bytes,
    size_t package_size,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const boot_slot_descriptor_t *slot,
    update_package_t *package,
    verify_status_t *verify_status
)
{
    update_package_t parsed;

    if (verify_status != NULL) {
        *verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    }

    if ((public_key == NULL) || (slot == NULL)) {
        return UPDATE_PACKAGE_ERR_INVALID_ARGUMENT;
    }

    update_package_status_t status =
        update_package_parse(package_bytes, package_size, &parsed);
    if (status != UPDATE_PACKAGE_OK) {
        return status;
    }

    if ((parsed.manifest.vector_address != slot->payload_base) ||
        (parsed.manifest.image_size > slot->maximum_payload_size)) {
        return UPDATE_PACKAGE_ERR_INCOMPATIBLE_SLOT;
    }

    const verify_status_t verified = signed_image_verify_update_slot_buffer(
        parsed.manifest_bytes,
        parsed.signature_bytes,
        parsed.payload,
        parsed.payload_size,
        public_key,
        slot
    );
    if (verify_status != NULL) {
        *verify_status = verified;
    }

    if (verified != VERIFY_OK) {
        return UPDATE_PACKAGE_ERR_VERIFY;
    }

    if (package != NULL) {
        *package = parsed;
    }

    return UPDATE_PACKAGE_OK;
}

const char *update_package_status_text(update_package_status_t status)
{
    switch (status) {
    case UPDATE_PACKAGE_OK:                      return "OK";
    case UPDATE_PACKAGE_ERR_INVALID_ARGUMENT:    return "INVALID ARGUMENT";
    case UPDATE_PACKAGE_ERR_TRUNCATED:           return "TRUNCATED";
    case UPDATE_PACKAGE_ERR_TRAILING_DATA:       return "TRAILING DATA";
    case UPDATE_PACKAGE_ERR_OVERSIZED:           return "OVERSIZED";
    case UPDATE_PACKAGE_ERR_UNSUPPORTED_VERSION: return "UNSUPPORTED VERSION";
    case UPDATE_PACKAGE_ERR_UNKNOWN_FLAGS:       return "UNKNOWN FLAGS";
    case UPDATE_PACKAGE_ERR_BAD_TARGET:          return "BAD TARGET";
    case UPDATE_PACKAGE_ERR_BAD_IMAGE_TYPE:      return "BAD IMAGE TYPE";
    case UPDATE_PACKAGE_ERR_BAD_SIZE:            return "BAD SIZE";
    case UPDATE_PACKAGE_ERR_BAD_PADDING:         return "BAD PADDING";
    case UPDATE_PACKAGE_ERR_INCOMPATIBLE_SLOT:   return "INCOMPATIBLE SLOT";
    case UPDATE_PACKAGE_ERR_VERIFY:              return "VERIFY";
    default:                                     return "UNKNOWN";
    }
}
