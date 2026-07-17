#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "monocypher-ed25519.h"
#include "signed_image.h"

#define TEST_PAYLOAD_SIZE 256U

static uint8_t manifest[SIGNED_MANIFEST_SIZE];
static uint8_t signature[STM32F429_SIGNED_SIGNATURE_SIZE];
static uint8_t payload[TEST_PAYLOAD_SIZE];
static uint8_t secret_key[64];
static uint8_t public_key[32];
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
    const uint8_t payload_hash[64]
)
{
    store_le32(&manifest[0], magic);
    store_le32(&manifest[4], header_version);
    store_le32(&manifest[8], image_version);
    store_le32(&manifest[12], vector_address);
    store_le32(&manifest[16], image_size);
    store_le32(&manifest[20], flags);
    store_le32(&manifest[24], reserved0);
    store_le32(&manifest[28], reserved1);
    memcpy(&manifest[32], payload_hash, 64U);
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
    uint8_t payload_hash[64];

    memset(payload, 0xA5, sizeof(payload));
    store_le32(&payload[0], msp);
    store_le32(&payload[4], reset);

    crypto_sha512(payload_hash, payload, (size_t)TEST_PAYLOAD_SIZE);
    write_manifest(
        SIGNED_IMAGE_MAGIC,
        SIGNED_HEADER_VERSION,
        image_version,
        APPLICATION_BASE_ADDRESS,
        image_size,
        flags,
        reserved0,
        reserved1,
        payload_hash
    );
    crypto_ed25519_sign(signature, secret_key, manifest, SIGNED_MANIFEST_SIZE);
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
}

static void test_valid_image(void)
{
    build_valid_image();
    expect_status("valid image", VERIFY_OK, verify_current(sizeof(payload)));
}

static void test_bad_magic(void)
{
    build_valid_image();
    manifest[0] ^= 0x01U;
    expect_status("bad magic", VERIFY_BAD_MAGIC, verify_current(sizeof(payload)));
}

static void test_unsupported_header_version(void)
{
    build_valid_image();
    store_le32(&manifest[4], SIGNED_HEADER_VERSION + 1UL);
    expect_status(
        "unsupported header version",
        VERIFY_BAD_HEADER_VERSION,
        verify_current(sizeof(payload))
    );
}

static void test_invalid_payload_length(void)
{
    build_valid_image();
    store_le32(&manifest[16], APPLICATION_MIN_SIZE - 1UL);
    expect_status(
        "invalid payload length",
        VERIFY_BAD_SIZE,
        verify_current(sizeof(payload))
    );
}

static void test_address_overflow(void)
{
    build_valid_image();
    store_le32(&manifest[16], UINT32_MAX);
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

int main(void)
{
    init_keys();

    test_enum_values_are_stable();
    test_valid_image();
    test_bad_magic();
    test_unsupported_header_version();
    test_invalid_payload_length();
    test_address_overflow();
    test_bad_msp();
    test_bad_reset_vector();
    test_reset_vector_outside_payload();
    test_modified_payload();
    test_modified_signature();
    test_unsupported_flags();
    test_noncanonical_reserved_fields();
    test_rollback_rejection();

    if (failures != 0) {
        printf("host verifier tests failed: %d\n", failures);
        return 1;
    }

    printf("host verifier tests passed\n");
    return 0;
}
