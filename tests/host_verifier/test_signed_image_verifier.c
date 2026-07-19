#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_slot.h"
#include "monocypher-ed25519.h"
#include "signed_image.h"
#include "stm32f429_memory_layout.h"

#define TEST_PAYLOAD_SIZE 256U

static uint8_t manifest[SIGNED_MANIFEST_SIZE];
static uint8_t signature[SIGNED_SIGNATURE_SIZE];
static uint8_t payload[MAX_PAYLOAD_SIZE];
static uint8_t secret_key[64];
static uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE];
static int failures;

static void store_le32(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    out[2] = (uint8_t)((value >> 16) & 0xFFU);
    out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void expect_int(const char *name, int expected, int actual)
{
    if (actual != expected) {
        printf("%s: expected %d, got %d\n", name, expected, actual);
        failures += 1;
    }
}

static void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
    if (actual != expected) {
        printf(
            "%s: expected 0x%08lx, got 0x%08lx\n",
            name,
            (unsigned long)expected,
            (unsigned long)actual
        );
        failures += 1;
    }
}

static void expect_status(
    const char *name,
    verify_status_t expected,
    verify_status_t actual
)
{
    if (actual != expected) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            signed_image_status_text(expected),
            (int)expected,
            signed_image_status_text(actual),
            (int)actual
        );
        failures += 1;
    }
}

static void init_keys(void)
{
    uint8_t seed[32];

    for (uint32_t i = 0U; i < sizeof(seed); ++i) {
        seed[i] = (uint8_t)i;
    }

    crypto_ed25519_key_pair(secret_key, public_key, seed);
}

static void write_manifest(
    uint32_t magic,
    uint32_t header_version,
    uint32_t image_version,
    uint32_t vector_address,
    uint32_t image_size,
    uint32_t flags,
    uint32_t reserved0,
    uint32_t reserved1,
    const uint8_t payload_hash[SIGNED_PAYLOAD_HASH_SIZE]
)
{
    store_le32(&manifest[offsetof(signed_manifest_t, magic)], magic);
    store_le32(
        &manifest[offsetof(signed_manifest_t, header_version)],
        header_version
    );
    store_le32(
        &manifest[offsetof(signed_manifest_t, image_version)],
        image_version
    );
    store_le32(
        &manifest[offsetof(signed_manifest_t, vector_address)],
        vector_address
    );
    store_le32(&manifest[offsetof(signed_manifest_t, image_size)], image_size);
    store_le32(&manifest[offsetof(signed_manifest_t, flags)], flags);
    store_le32(&manifest[offsetof(signed_manifest_t, reserved0)], reserved0);
    store_le32(&manifest[offsetof(signed_manifest_t, reserved1)], reserved1);
    memcpy(
        &manifest[offsetof(signed_manifest_t, payload_sha512)],
        payload_hash,
        SIGNED_PAYLOAD_HASH_SIZE
    );
}

static size_t bounded_payload_length(uint32_t image_size)
{
    if (image_size > (uint32_t)sizeof(payload)) {
        return sizeof(payload);
    }

    return (size_t)image_size;
}

static void build_image_with_vector_address(
    uint32_t image_version,
    uint32_t vector_address,
    uint32_t image_size,
    uint32_t msp,
    uint32_t reset,
    uint32_t flags,
    uint32_t reserved0,
    uint32_t reserved1
)
{
    uint8_t payload_hash[SIGNED_PAYLOAD_HASH_SIZE];
    const size_t hash_length = bounded_payload_length(image_size);
    size_t init_length = hash_length;

    if (init_length < APPLICATION_VECTOR_MIN_SIZE) {
        init_length = APPLICATION_VECTOR_MIN_SIZE;
    }

    memset(payload, 0xA5, init_length);
    store_le32(&payload[0], msp);
    store_le32(&payload[4], reset);

    crypto_sha512(payload_hash, payload, hash_length);
    write_manifest(
        SIGNED_IMAGE_MAGIC,
        SIGNED_HEADER_VERSION,
        image_version,
        vector_address,
        image_size,
        flags,
        reserved0,
        reserved1,
        payload_hash
    );
    crypto_ed25519_sign(signature, secret_key, manifest, SIGNED_MANIFEST_SIZE);
}

static void build_image(
    uint32_t image_version,
    uint32_t image_size,
    uint32_t msp,
    uint32_t reset,
    uint32_t flags,
    uint32_t reserved0,
    uint32_t reserved1
)
{
    build_image_with_vector_address(
        image_version,
        APPLICATION_BASE_ADDRESS,
        image_size,
        msp,
        reset,
        flags,
        reserved0,
        reserved1
    );
}

static void build_valid_image(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
}

static verify_status_t verify_current(size_t payload_capacity)
{
    return signed_image_verify_buffer(
        manifest,
        signature,
        payload,
        payload_capacity,
        public_key
    );
}

static signed_image_jump_context_t valid_jump_context(uint32_t image_size)
{
    signed_image_jump_context_t context;

    context.vector_address = APPLICATION_BASE_ADDRESS;
    context.image_size = image_size;
    context.payload_end = APPLICATION_BASE_ADDRESS + image_size;
    context.initial_msp = APPLICATION_MSP_END;
    context.reset_vector = APPLICATION_BASE_ADDRESS | 1UL;
    context.reset_address = APPLICATION_BASE_ADDRESS;

    return context;
}

static void test_enum_values_are_stable(void)
{
    expect_int("VERIFY_OK", 0, (int)VERIFY_OK);
    expect_int("VERIFY_BAD_MAGIC", 1, (int)VERIFY_BAD_MAGIC);
    expect_int("VERIFY_BAD_HEADER_VERSION", 2, (int)VERIFY_BAD_HEADER_VERSION);
    expect_int("VERIFY_ROLLBACK_VERSION", 3, (int)VERIFY_ROLLBACK_VERSION);
    expect_int("VERIFY_BAD_VECTOR_ADDRESS", 4, (int)VERIFY_BAD_VECTOR_ADDRESS);
    expect_int("VERIFY_BAD_SIZE", 5, (int)VERIFY_BAD_SIZE);
    expect_int("VERIFY_BAD_STACK", 6, (int)VERIFY_BAD_STACK);
    expect_int("VERIFY_BAD_RESET_VECTOR", 7, (int)VERIFY_BAD_RESET_VECTOR);
    expect_int("VERIFY_BAD_PAYLOAD_HASH", 8, (int)VERIFY_BAD_PAYLOAD_HASH);
    expect_int("VERIFY_BAD_SIGNATURE", 9, (int)VERIFY_BAD_SIGNATURE);
    expect_int("VERIFY_BAD_FLAGS", 10, (int)VERIFY_BAD_FLAGS);
    expect_int("VERIFY_BAD_RESERVED", 11, (int)VERIFY_BAD_RESERVED);
    expect_int("VERIFY_BAD_PAYLOAD_RANGE", 12, (int)VERIFY_BAD_PAYLOAD_RANGE);
    expect_int(
        "VERIFY_BAD_TARGET_COMPATIBILITY",
        13,
        (int)VERIFY_BAD_TARGET_COMPATIBILITY
    );
    expect_int("VERIFY_BAD_IMAGE_TYPE", 14, (int)VERIFY_BAD_IMAGE_TYPE);
}

static void test_boot_slot_descriptors(void)
{
    const boot_slot_descriptor_t *slot = NULL;

    expect_int(
        "slot A lookup",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_A, &slot)
    );
    if (slot != NULL) {
        expect_int("slot A id", BOOT_SLOT_A, (int)slot->id);
        expect_u32(
            "slot A signed image",
            STM32F429_SLOT_A_SIGNED_IMAGE_BASE,
            slot->signed_image_base
        );
        expect_u32(
            "slot A manifest",
            STM32F429_SLOT_A_MANIFEST_BASE,
            slot->manifest_address
        );
        expect_u32(
            "slot A signature",
            STM32F429_SLOT_A_SIGNATURE_BASE,
            slot->signature_address
        );
        expect_u32(
            "slot A payload",
            STM32F429_SLOT_A_PAYLOAD_BASE,
            slot->payload_base
        );
        expect_u32("slot A end", STM32F429_SLOT_A_END, slot->slot_end);
        expect_u32(
            "slot A payload max",
            STM32F429_SLOT_A_PAYLOAD_MAX_SIZE,
            slot->maximum_payload_size
        );
        expect_u32(
            "slot A first sector",
            STM32F429_SLOT_A_FIRST_SECTOR,
            slot->first_sector
        );
        expect_u32(
            "slot A last sector",
            STM32F429_SLOT_A_LAST_SECTOR,
            slot->last_sector
        );
    }

    expect_int(
        "slot B lookup",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_B, &slot)
    );
    if (slot != NULL) {
        expect_int("slot B id", BOOT_SLOT_B, (int)slot->id);
        expect_u32(
            "slot B signed image",
            STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
            slot->signed_image_base
        );
        expect_u32(
            "slot B manifest",
            STM32F429_SLOT_B_MANIFEST_BASE,
            slot->manifest_address
        );
        expect_u32(
            "slot B signature",
            STM32F429_SLOT_B_SIGNATURE_BASE,
            slot->signature_address
        );
        expect_u32(
            "slot B payload",
            STM32F429_SLOT_B_PAYLOAD_BASE,
            slot->payload_base
        );
        expect_u32("slot B end", STM32F429_SLOT_B_END, slot->slot_end);
        expect_u32(
            "slot B payload max",
            STM32F429_SLOT_B_PAYLOAD_MAX_SIZE,
            slot->maximum_payload_size
        );
        expect_u32(
            "slot B first sector",
            STM32F429_SLOT_B_FIRST_SECTOR,
            slot->first_sector
        );
        expect_u32(
            "slot B last sector",
            STM32F429_SLOT_B_LAST_SECTOR,
            slot->last_sector
        );
    }

    expect_int(
        "invalid slot lookup",
        BOOT_SLOT_LOOKUP_INVALID,
        boot_slot_lookup(2U, &slot)
    );
    if (slot != NULL) {
        printf("invalid slot lookup returned a descriptor\n");
        failures += 1;
    }

    expect_int(
        "null slot lookup",
        BOOT_SLOT_LOOKUP_INVALID,
        boot_slot_lookup(BOOT_SLOT_A, NULL)
    );

    slot = boot_slot_default();
    if (slot == NULL) {
        printf("default slot is null\n");
        failures += 1;
    } else {
        expect_int("default slot is A", BOOT_SLOT_A, (int)slot->id);
    }
}

static void test_valid_image(void)
{
    build_valid_image();
    expect_status("valid image", VERIFY_OK, verify_current(TEST_PAYLOAD_SIZE));
}

static void test_null_inputs_are_rejected(void)
{
    build_valid_image();
    expect_status(
        "null manifest",
        VERIFY_BAD_PAYLOAD_RANGE,
        signed_image_verify_buffer(
            NULL,
            signature,
            payload,
            TEST_PAYLOAD_SIZE,
            public_key
        )
    );
    expect_status(
        "null signature",
        VERIFY_BAD_PAYLOAD_RANGE,
        signed_image_verify_buffer(
            manifest,
            NULL,
            payload,
            TEST_PAYLOAD_SIZE,
            public_key
        )
    );
    expect_status(
        "null payload",
        VERIFY_BAD_PAYLOAD_RANGE,
        signed_image_verify_buffer(
            manifest,
            signature,
            NULL,
            TEST_PAYLOAD_SIZE,
            public_key
        )
    );
    expect_status(
        "null public key",
        VERIFY_BAD_PAYLOAD_RANGE,
        signed_image_verify_buffer(
            manifest,
            signature,
            payload,
            TEST_PAYLOAD_SIZE,
            NULL
        )
    );
}

static void test_minimum_payload_length(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        APPLICATION_VECTOR_MIN_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "minimum payload length",
        VERIFY_OK,
        verify_current(APPLICATION_VECTOR_MIN_SIZE)
    );
}

static void test_payload_capacity_one_byte_short(void)
{
    build_valid_image();
    expect_status(
        "payload capacity one byte short",
        VERIFY_BAD_PAYLOAD_RANGE,
        verify_current(TEST_PAYLOAD_SIZE - 1U)
    );
}

static void test_maximum_payload_length(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        MAX_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "maximum payload length",
        VERIFY_OK,
        verify_current(MAX_PAYLOAD_SIZE)
    );
}

static void test_payload_one_byte_too_large(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        MAX_PAYLOAD_SIZE + 1UL,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "payload one byte too large",
        VERIFY_BAD_SIZE,
        verify_current(MAX_PAYLOAD_SIZE)
    );
}

static void test_jump_context_revalidation(void)
{
    signed_image_jump_context_t context = valid_jump_context(TEST_PAYLOAD_SIZE);

    expect_status(
        "valid jump context",
        VERIFY_OK,
        signed_image_jump(&context)
    );
    expect_status(
        "null jump context",
        VERIFY_BAD_PAYLOAD_RANGE,
        signed_image_jump(NULL)
    );

    context = valid_jump_context(TEST_PAYLOAD_SIZE);
    context.initial_msp = APPLICATION_MSP_END - 4UL;
    expect_status(
        "jump context bad MSP",
        VERIFY_BAD_STACK,
        signed_image_jump(&context)
    );

    context = valid_jump_context(TEST_PAYLOAD_SIZE);
    context.reset_vector = APPLICATION_BASE_ADDRESS;
    expect_status(
        "jump context bad reset",
        VERIFY_BAD_RESET_VECTOR,
        signed_image_jump(&context)
    );

    context = valid_jump_context(TEST_PAYLOAD_SIZE);
    context.payload_end += APPLICATION_MSP_ALIGNMENT;
    expect_status(
        "jump context inconsistent payload end",
        VERIFY_BAD_PAYLOAD_RANGE,
        signed_image_jump(&context)
    );

    context = valid_jump_context(TEST_PAYLOAD_SIZE);
    context.vector_address += APPLICATION_VTOR_ALIGNMENT;
    expect_status(
        "jump context bad vector address",
        VERIFY_BAD_VECTOR_ADDRESS,
        signed_image_jump(&context)
    );
}

static void test_bad_magic(void)
{
    build_valid_image();
    manifest[offsetof(signed_manifest_t, magic)] ^= 0x01U;
    expect_status("bad magic", VERIFY_BAD_MAGIC, verify_current(sizeof(payload)));
}

static void test_unsupported_header_version(void)
{
    build_valid_image();
    store_le32(
        &manifest[offsetof(signed_manifest_t, header_version)],
        SIGNED_HEADER_VERSION + 1UL
    );
    expect_status(
        "unsupported header version",
        VERIFY_BAD_HEADER_VERSION,
        verify_current(sizeof(payload))
    );
}

static void test_invalid_payload_length(void)
{
    build_valid_image();
    store_le32(
        &manifest[offsetof(signed_manifest_t, image_size)],
        APPLICATION_MIN_SIZE - 1UL
    );
    expect_status(
        "invalid payload length",
        VERIFY_BAD_SIZE,
        verify_current(sizeof(payload))
    );
}

static void test_bad_vector_address(void)
{
    build_image_with_vector_address(
        MIN_IMAGE_VERSION,
        APPLICATION_BASE_ADDRESS + APPLICATION_VTOR_ALIGNMENT,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "bad manifest vector address",
        VERIFY_BAD_VECTOR_ADDRESS,
        verify_current(TEST_PAYLOAD_SIZE)
    );
}

static void test_address_overflow(void)
{
    build_valid_image();
    store_le32(&manifest[offsetof(signed_manifest_t, image_size)], UINT32_MAX);
    expect_status(
        "address overflow",
        VERIFY_BAD_PAYLOAD_RANGE,
        verify_current(sizeof(payload))
    );
}

static void test_bad_msp(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_BASE,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status("bad MSP", VERIFY_BAD_STACK, verify_current(sizeof(payload)));
}

static void test_lowest_aligned_msp(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_BASE + APPLICATION_MSP_ALIGNMENT,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "lowest aligned MSP",
        VERIFY_OK,
        verify_current(TEST_PAYLOAD_SIZE)
    );
}

static void test_highest_msp(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status("highest MSP", VERIFY_OK, verify_current(TEST_PAYLOAD_SIZE));
}

static void test_msp_above_supported_sram(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END + APPLICATION_MSP_ALIGNMENT,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "MSP above supported SRAM",
        VERIFY_BAD_STACK,
        verify_current(sizeof(payload))
    );
}

static void test_unaligned_msp(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END - 4UL,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "unaligned MSP",
        VERIFY_BAD_STACK,
        verify_current(sizeof(payload))
    );
}

static void test_bad_reset_vector(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS,
        0U,
        0U,
        0U
    );
    expect_status(
        "bad reset vector",
        VERIFY_BAD_RESET_VECTOR,
        verify_current(sizeof(payload))
    );
}

static void test_last_payload_reset_vector(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        (APPLICATION_BASE_ADDRESS + TEST_PAYLOAD_SIZE - 2UL) | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "last payload reset vector",
        VERIFY_OK,
        verify_current(TEST_PAYLOAD_SIZE)
    );
}

static void test_reset_vector_before_payload(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        (APPLICATION_BASE_ADDRESS - 2UL) | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "reset before payload",
        VERIFY_BAD_RESET_VECTOR,
        verify_current(sizeof(payload))
    );
}

static void test_reset_vector_outside_payload(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        (APPLICATION_BASE_ADDRESS + TEST_PAYLOAD_SIZE) | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "reset outside payload",
        VERIFY_BAD_RESET_VECTOR,
        verify_current(sizeof(payload))
    );
}

static void test_reset_vector_at_flash_end(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        MAX_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_FLASH_END | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "reset at flash end",
        VERIFY_BAD_RESET_VECTOR,
        verify_current(MAX_PAYLOAD_SIZE)
    );
}

static void test_modified_payload(void)
{
    build_valid_image();
    payload[16] ^= 0x80U;
    expect_status(
        "modified payload",
        VERIFY_BAD_PAYLOAD_HASH,
        verify_current(sizeof(payload))
    );
}

static void test_modified_signature(void)
{
    build_valid_image();
    signature[0] ^= 0x01U;
    expect_status(
        "modified signature",
        VERIFY_BAD_SIGNATURE,
        verify_current(sizeof(payload))
    );
}

static void test_unsupported_flags(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        1U,
        0U,
        0U
    );
    expect_status(
        "unsupported flags",
        VERIFY_BAD_FLAGS,
        verify_current(sizeof(payload))
    );
}

static void test_unsupported_high_flag_bit(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0x80000000UL,
        0U,
        0U
    );
    expect_status(
        "unsupported high flag bit",
        VERIFY_BAD_FLAGS,
        verify_current(sizeof(payload))
    );
}

static void test_noncanonical_reserved_fields(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        1U,
        0U
    );
    expect_status(
        "noncanonical reserved fields",
        VERIFY_BAD_RESERVED,
        verify_current(sizeof(payload))
    );
}

static void test_noncanonical_reserved1(void)
{
    build_image(
        MIN_IMAGE_VERSION,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        1U
    );
    expect_status(
        "noncanonical reserved1",
        VERIFY_BAD_RESERVED,
        verify_current(sizeof(payload))
    );
}

static void test_rollback_rejection(void)
{
    build_image(
        MIN_IMAGE_VERSION - 1UL,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        APPLICATION_BASE_ADDRESS | 1UL,
        0U,
        0U,
        0U
    );
    expect_status(
        "rollback rejection",
        VERIFY_ROLLBACK_VERSION,
        verify_current(sizeof(payload))
    );
}

static void test_update_package_slot_b_verification(void)
{
    const boot_slot_descriptor_t *slot_b = NULL;
    const boot_slot_descriptor_t *slot_a = NULL;

    expect_int(
        "slot B for update package",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_B, &slot_b)
    );
    expect_int(
        "slot A for update package mismatch",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_A, &slot_a)
    );

    build_image_with_vector_address(
        MIN_IMAGE_VERSION + 1UL,
        STM32F429_SLOT_B_PAYLOAD_BASE,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        STM32F429_SLOT_B_PAYLOAD_BASE | 1UL,
        0U,
        UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1,
        UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION
    );
    store_le32(
        &manifest[offsetof(signed_manifest_t, header_version)],
        UPDATE_PACKAGE_FORMAT_VERSION
    );
    crypto_ed25519_sign(signature, secret_key, manifest, SIGNED_MANIFEST_SIZE);

    expect_status(
        "update package slot B",
        VERIFY_OK,
        signed_image_verify_update_slot_buffer(
            manifest,
            signature,
            payload,
            TEST_PAYLOAD_SIZE,
            public_key,
            slot_b
        )
    );
    expect_status(
        "v2 package rejected by legacy verifier",
        VERIFY_BAD_HEADER_VERSION,
        signed_image_verify_buffer(
            manifest,
            signature,
            payload,
            TEST_PAYLOAD_SIZE,
            public_key
        )
    );
    expect_status(
        "slot B package rejected for slot A",
        VERIFY_BAD_VECTOR_ADDRESS,
        signed_image_verify_update_slot_buffer(
            manifest,
            signature,
            payload,
            TEST_PAYLOAD_SIZE,
            public_key,
            slot_a
        )
    );
}

static void test_update_package_rejects_bad_authenticated_fields(void)
{
    const boot_slot_descriptor_t *slot_b = NULL;

    expect_int(
        "slot B for bad authenticated fields",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_B, &slot_b)
    );

    build_image_with_vector_address(
        MIN_IMAGE_VERSION + 1UL,
        STM32F429_SLOT_B_PAYLOAD_BASE,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        STM32F429_SLOT_B_PAYLOAD_BASE | 1UL,
        0U,
        UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1 + 1UL,
        UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION
    );
    store_le32(
        &manifest[offsetof(signed_manifest_t, header_version)],
        UPDATE_PACKAGE_FORMAT_VERSION
    );
    crypto_ed25519_sign(signature, secret_key, manifest, SIGNED_MANIFEST_SIZE);
    expect_status(
        "bad update target compatibility",
        VERIFY_BAD_TARGET_COMPATIBILITY,
        signed_image_verify_update_slot_buffer(
            manifest,
            signature,
            payload,
            TEST_PAYLOAD_SIZE,
            public_key,
            slot_b
        )
    );

    build_image_with_vector_address(
        MIN_IMAGE_VERSION + 1UL,
        STM32F429_SLOT_B_PAYLOAD_BASE,
        TEST_PAYLOAD_SIZE,
        APPLICATION_MSP_END,
        STM32F429_SLOT_B_PAYLOAD_BASE | 1UL,
        0U,
        UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1,
        UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION + 1UL
    );
    store_le32(
        &manifest[offsetof(signed_manifest_t, header_version)],
        UPDATE_PACKAGE_FORMAT_VERSION
    );
    crypto_ed25519_sign(signature, secret_key, manifest, SIGNED_MANIFEST_SIZE);
    expect_status(
        "bad update image type",
        VERIFY_BAD_IMAGE_TYPE,
        signed_image_verify_update_slot_buffer(
            manifest,
            signature,
            payload,
            TEST_PAYLOAD_SIZE,
            public_key,
            slot_b
        )
    );
}

int main(void)
{
    init_keys();

    test_enum_values_are_stable();
    test_boot_slot_descriptors();
    test_valid_image();
    test_null_inputs_are_rejected();
    test_minimum_payload_length();
    test_payload_capacity_one_byte_short();
    test_maximum_payload_length();
    test_payload_one_byte_too_large();
    test_jump_context_revalidation();
    test_bad_magic();
    test_unsupported_header_version();
    test_invalid_payload_length();
    test_bad_vector_address();
    test_address_overflow();
    test_bad_msp();
    test_lowest_aligned_msp();
    test_highest_msp();
    test_msp_above_supported_sram();
    test_unaligned_msp();
    test_bad_reset_vector();
    test_last_payload_reset_vector();
    test_reset_vector_before_payload();
    test_reset_vector_outside_payload();
    test_reset_vector_at_flash_end();
    test_modified_payload();
    test_modified_signature();
    test_unsupported_flags();
    test_unsupported_high_flag_bit();
    test_noncanonical_reserved_fields();
    test_noncanonical_reserved1();
    test_rollback_rejection();
    test_update_package_slot_b_verification();
    test_update_package_rejects_bad_authenticated_fields();

    if (failures != 0) {
        printf("host verifier tests failed: %d\n", failures);
        return 1;
    }

    printf("host verifier tests passed\n");
    return 0;
}
