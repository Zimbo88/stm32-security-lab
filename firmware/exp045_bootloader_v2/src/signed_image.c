#include <stddef.h>
#include <stdint.h>

#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "performance.h"
#include "signed_image.h"

#ifndef SIGNED_IMAGE_HOST_TEST
#include "firmware_public_key.h"
#endif

_Static_assert(
    sizeof(signed_manifest_t) == SIGNED_MANIFEST_SIZE,
    "signed manifest structure must match serialized manifest size"
);

_Static_assert(
    (APPLICATION_MSP_ALIGNMENT != 0UL) &&
    ((APPLICATION_MSP_ALIGNMENT & (APPLICATION_MSP_ALIGNMENT - 1UL)) == 0UL),
    "application MSP alignment must be a nonzero power of two"
);

#define REG32(a) (*(volatile uint32_t *)(a))

#define SCB_VTOR       REG32(0xE000ED08UL)
#define SYST_CSR       REG32(0xE000E010UL)
#define NVIC_ICER_BASE 0xE000E180UL
#define NVIC_ICPR_BASE 0xE000E280UL

typedef void (*entry_fn_t)(void);

#ifndef SIGNED_IMAGE_HOST_TEST
static const uint8_t *signature(void)
{
    return (const uint8_t *)SIGNATURE_ADDRESS;
}

static const signed_manifest_t *manifest(void)
{
    return (const signed_manifest_t *)SIGNED_IMAGE_BASE;
}
#endif

static uint32_t load_le32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0]) |
           (((uint32_t)bytes[1]) << 8) |
           (((uint32_t)bytes[2]) << 16) |
           (((uint32_t)bytes[3]) << 24);
}

static void decode_manifest(
    const uint8_t bytes[SIGNED_MANIFEST_SIZE],
    signed_manifest_t *m
)
{
    m->magic = load_le32(&bytes[0]);
    m->header_version = load_le32(&bytes[4]);
    m->image_version = load_le32(&bytes[8]);
    m->vector_address = load_le32(&bytes[12]);
    m->image_size = load_le32(&bytes[16]);
    m->flags = load_le32(&bytes[20]);
    m->reserved0 = load_le32(&bytes[24]);
    m->reserved1 = load_le32(&bytes[28]);

    for (size_t i = 0U; i < sizeof(m->payload_sha512); ++i) {
        m->payload_sha512[i] = bytes[32U + i];
    }
}

static uint8_t checked_u32_add(uint32_t left, uint32_t right, uint32_t *out)
{
    if (right > (UINT32_MAX - left)) {
        return 0U;
    }

    *out = left + right;
    return 1U;
}

static verify_status_t validate_manifest_header(const signed_manifest_t *m)
{
    if (sizeof(signed_manifest_t) != SIGNED_MANIFEST_SIZE) {
        return VERIFY_BAD_HEADER_VERSION;
    }

    if (m->magic != SIGNED_IMAGE_MAGIC) {
        return VERIFY_BAD_MAGIC;
    }

    if (m->header_version != SIGNED_HEADER_VERSION) {
        return VERIFY_BAD_HEADER_VERSION;
    }

    const uint32_t unsupported_flags =
        m->flags & ~((uint32_t)SIGNED_IMAGE_FLAGS_ALLOWED_MASK);

    if (unsupported_flags != 0UL) {
        return VERIFY_BAD_FLAGS;
    }

    if ((m->reserved0 != 0UL) || (m->reserved1 != 0UL)) {
        return VERIFY_BAD_RESERVED;
    }

    if (m->image_version < MIN_IMAGE_VERSION) {
        return VERIFY_ROLLBACK_VERSION;
    }

    return VERIFY_OK;
}

static verify_status_t validate_payload_range(
    const signed_manifest_t *m,
    uint32_t *payload_end
)
{
    if (m->vector_address != APPLICATION_BASE_ADDRESS) {
        return VERIFY_BAD_VECTOR_ADDRESS;
    }

    if (m->image_size < APPLICATION_MIN_SIZE) {
        return VERIFY_BAD_SIZE;
    }

    if (checked_u32_add(m->vector_address, m->image_size, payload_end) == 0U) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if (m->image_size > MAX_PAYLOAD_SIZE) {
        return VERIFY_BAD_SIZE;
    }

    if (*payload_end > APPLICATION_FLASH_END) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    return VERIFY_OK;
}

static verify_status_t validate_vector_table(
    const signed_manifest_t *m,
    const uint8_t *payload,
    uint32_t payload_end
)
{
    const uint32_t msp = load_le32(&payload[0]);
    const uint32_t reset = load_le32(&payload[4]);
    const uint32_t reset_address = reset & ~1UL;

    if ((msp <= APPLICATION_MSP_BASE) ||
        (msp > APPLICATION_MSP_END) ||
        ((msp & (APPLICATION_MSP_ALIGNMENT - 1UL)) != 0UL)) {
        return VERIFY_BAD_STACK;
    }

    if (((reset & 1UL) == 0U) ||
        (reset_address < m->vector_address) ||
        (reset_address >= payload_end) ||
        (reset_address >= APPLICATION_FLASH_END)) {
        return VERIFY_BAD_RESET_VECTOR;
    }

    return VERIFY_OK;
}

verify_status_t signed_image_verify_buffer(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    const uint8_t signature_bytes[STM32F429_SIGNED_SIGNATURE_SIZE],
    const uint8_t *payload,
    size_t payload_capacity,
    const uint8_t public_key[32]
)
{
    signed_manifest_t decoded_manifest;
    decode_manifest(manifest_bytes, &decoded_manifest);

    const signed_manifest_t *m = &decoded_manifest;
    uint32_t payload_end = 0U;
    verify_status_t status = validate_manifest_header(m);

    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_payload_range(m, &payload_end);
    if (status != VERIFY_OK) {
        return status;
    }

    if ((size_t)m->image_size > payload_capacity) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    status = validate_vector_table(m, payload, payload_end);
    if (status != VERIFY_OK) {
        return status;
    }

    uint8_t computed_hash[64];
    const uint32_t sha512_start = performance_cycles();

    crypto_sha512(
        computed_hash,
        payload,
        (size_t)m->image_size
    );

    const uint32_t sha512_end = performance_cycles();
    performance_record_sha512_cycles(sha512_end - sha512_start);

    if (crypto_verify64(computed_hash, m->payload_sha512) != 0) {
        crypto_wipe(computed_hash, sizeof(computed_hash));
        return VERIFY_BAD_PAYLOAD_HASH;
    }

    crypto_wipe(computed_hash, sizeof(computed_hash));

    const uint32_t ed25519_start = performance_cycles();

    const int ed25519_result = crypto_ed25519_check(
        signature_bytes,
        public_key,
        manifest_bytes,
        (size_t)SIGNED_MANIFEST_SIZE
    );

    const uint32_t ed25519_end = performance_cycles();
    performance_record_ed25519_cycles(ed25519_end - ed25519_start);

    if (ed25519_result != 0) {
        return VERIFY_BAD_SIGNATURE;
    }

    return VERIFY_OK;
}

const char *signed_image_status_text(verify_status_t status)
{
    switch (status) {
    case VERIFY_OK:                 return "OK";
    case VERIFY_BAD_MAGIC:          return "BAD MAGIC";
    case VERIFY_BAD_HEADER_VERSION: return "BAD HEADER VERSION";
    case VERIFY_ROLLBACK_VERSION:   return "ROLLBACK VERSION REJECTED";
    case VERIFY_BAD_VECTOR_ADDRESS: return "BAD VECTOR ADDRESS";
    case VERIFY_BAD_SIZE:           return "BAD IMAGE SIZE";
    case VERIFY_BAD_STACK:          return "BAD INITIAL MSP";
    case VERIFY_BAD_RESET_VECTOR:   return "BAD RESET VECTOR";
    case VERIFY_BAD_PAYLOAD_HASH:   return "PAYLOAD SHA512 MISMATCH";
    case VERIFY_BAD_SIGNATURE:      return "ED25519 SIGNATURE INVALID";
    case VERIFY_BAD_FLAGS:          return "UNSUPPORTED MANIFEST FLAGS";
    case VERIFY_BAD_RESERVED:       return "NONZERO MANIFEST RESERVED FIELD";
    case VERIFY_BAD_PAYLOAD_RANGE:  return "BAD PAYLOAD RANGE";
    default:                        return "UNKNOWN";
    }
}

#ifndef SIGNED_IMAGE_HOST_TEST
verify_status_t signed_image_verify(void)
{
    return signed_image_verify_buffer(
        (const uint8_t *)SIGNED_IMAGE_BASE,
        signature(),
        (const uint8_t *)APPLICATION_BASE_ADDRESS,
        (size_t)MAX_PAYLOAD_SIZE,
        firmware_public_key
    );
}
#endif

void signed_image_jump(void)
{
#ifndef SIGNED_IMAGE_HOST_TEST
    const signed_manifest_t *m = manifest();
    const uint32_t new_msp =
        *(volatile const uint32_t *)(m->vector_address + 0U);
    const uint32_t reset =
        *(volatile const uint32_t *)(m->vector_address + 4U);
    const entry_fn_t entry = (entry_fn_t)reset;

    __asm volatile ("cpsid i" ::: "memory");

    SYST_CSR = 0U;

    for (uint32_t i = 0U; i < 8U; ++i) {
        REG32(NVIC_ICER_BASE + (i * 4U)) = 0xFFFFFFFFUL;
        REG32(NVIC_ICPR_BASE + (i * 4U)) = 0xFFFFFFFFUL;
    }

    SCB_VTOR = m->vector_address;

    __asm volatile (
        "dsb\n"
        "isb\n"
        "msr msp, %0\n"
        "cpsie i\n"
        :
        : "r" (new_msp)
        : "memory"
    );

    entry();

    for (;;) {
    }
#else
    for (;;) {
    }
#endif
}
