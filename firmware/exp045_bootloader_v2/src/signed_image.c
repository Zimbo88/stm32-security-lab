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

_Static_assert(offsetof(signed_manifest_t, magic) == 0U, "bad magic offset");
_Static_assert(
    offsetof(signed_manifest_t, header_version) == 4U,
    "bad header version offset"
);
_Static_assert(
    offsetof(signed_manifest_t, image_version) == 8U,
    "bad image version offset"
);
_Static_assert(
    offsetof(signed_manifest_t, vector_address) == 12U,
    "bad vector address offset"
);
_Static_assert(
    offsetof(signed_manifest_t, image_size) == 16U,
    "bad image size offset"
);
_Static_assert(offsetof(signed_manifest_t, flags) == 20U, "bad flags offset");
_Static_assert(
    offsetof(signed_manifest_t, reserved0) == 24U,
    "bad reserved0 offset"
);
_Static_assert(
    offsetof(signed_manifest_t, reserved1) == 28U,
    "bad reserved1 offset"
);
_Static_assert(
    offsetof(signed_manifest_t, payload_sha512) == 32U,
    "bad payload hash offset"
);
_Static_assert(
    sizeof(((signed_manifest_t *)0)->payload_sha512) ==
        SIGNED_PAYLOAD_HASH_SIZE,
    "payload hash must be SHA-512 sized"
);
_Static_assert(
    SIGNED_PAYLOAD_HASH_SIZE == 64U,
    "SHA-512 payload hash size must remain 64 bytes"
);
_Static_assert(
    SIGNED_SIGNATURE_SIZE == 64UL,
    "Ed25519 signature size must remain 64 bytes"
);
_Static_assert(
    SIGNED_IMAGE_HEADER_SIZE >= (SIGNED_MANIFEST_SIZE + SIGNED_SIGNATURE_SIZE),
    "signed image header must contain manifest and signature"
);
_Static_assert(
    (SIGNED_IMAGE_BASE + SIGNED_MANIFEST_SIZE) == SIGNATURE_ADDRESS,
    "signature address must follow manifest"
);
_Static_assert(
    (SIGNED_IMAGE_BASE + SIGNED_IMAGE_HEADER_SIZE) == APPLICATION_BASE_ADDRESS,
    "application payload must follow signed image header"
);
_Static_assert(
    MAX_PAYLOAD_SIZE ==
        (APPLICATION_FLASH_END - APPLICATION_BASE_ADDRESS),
    "maximum payload size must match application flash region"
);
_Static_assert(
    APPLICATION_MIN_SIZE >= APPLICATION_VECTOR_MIN_SIZE,
    "minimum application size must contain initial MSP and reset vector"
);
_Static_assert(
    (APPLICATION_VTOR_ALIGNMENT != 0UL) &&
    ((APPLICATION_VTOR_ALIGNMENT & (APPLICATION_VTOR_ALIGNMENT - 1UL)) == 0UL),
    "VTOR alignment must be a nonzero power of two"
);
_Static_assert(
    (APPLICATION_BASE_ADDRESS & (APPLICATION_VTOR_ALIGNMENT - 1UL)) == 0UL,
    "application base must satisfy VTOR alignment"
);
_Static_assert(
    (APPLICATION_MSP_ALIGNMENT != 0UL) &&
    ((APPLICATION_MSP_ALIGNMENT & (APPLICATION_MSP_ALIGNMENT - 1UL)) == 0UL),
    "application MSP alignment must be a nonzero power of two"
);

#ifndef SIGNED_IMAGE_HOST_TEST
_Static_assert(
    sizeof(firmware_public_key) == FIRMWARE_PUBLIC_KEY_SIZE,
    "firmware public key size must match verifier policy"
);
#endif

#define REG32(a) (*(volatile uint32_t *)(a))

#define SCB_VTOR       REG32(0xE000ED08UL)
#define SYST_CSR       REG32(0xE000E010UL)
#define NVIC_ICER_BASE 0xE000E180UL
#define NVIC_ICPR_BASE 0xE000E280UL

typedef void (*entry_fn_t)(void);

typedef struct {
    uint32_t initial_msp;
    uint32_t reset_vector;
    uint32_t reset_address;
} application_vector_t;

typedef struct {
    uint32_t header_version;
    uint32_t vector_address;
    uint32_t payload_flash_end;
    uint32_t maximum_payload_size;
    uint32_t allowed_flags_mask;
    uint32_t target_compatibility;
    uint32_t image_type;
    uint8_t require_update_fields;
} signed_image_validation_policy_t;

#ifndef SIGNED_IMAGE_HOST_TEST
static const uint8_t *signature(void)
{
    return (const uint8_t *)SIGNATURE_ADDRESS;
}
#endif

static uint8_t u32_equal_redundant(uint32_t left, uint32_t right)
{
    return ((left == right) && ((left ^ right) == 0UL)) ? 1U : 0U;
}

static uint32_t load_le32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0]) |
           (((uint32_t)bytes[1]) << 8) |
           (((uint32_t)bytes[2]) << 16) |
           (((uint32_t)bytes[3]) << 24);
}

static void decode_manifest_bytes(
    const uint8_t bytes[SIGNED_MANIFEST_SIZE],
    signed_manifest_t *m
)
{
    m->magic = load_le32(&bytes[offsetof(signed_manifest_t, magic)]);
    m->header_version =
        load_le32(&bytes[offsetof(signed_manifest_t, header_version)]);
    m->image_version =
        load_le32(&bytes[offsetof(signed_manifest_t, image_version)]);
    m->vector_address =
        load_le32(&bytes[offsetof(signed_manifest_t, vector_address)]);
    m->image_size = load_le32(&bytes[offsetof(signed_manifest_t, image_size)]);
    m->flags = load_le32(&bytes[offsetof(signed_manifest_t, flags)]);
    m->reserved0 = load_le32(&bytes[offsetof(signed_manifest_t, reserved0)]);
    m->reserved1 = load_le32(&bytes[offsetof(signed_manifest_t, reserved1)]);

    for (size_t i = 0U; i < sizeof(m->payload_sha512); ++i) {
        m->payload_sha512[i] =
            bytes[offsetof(signed_manifest_t, payload_sha512) + i];
    }
}

verify_status_t signed_image_decode_manifest(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    signed_manifest_t *manifest
)
{
    if ((manifest_bytes == NULL) || (manifest == NULL)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    decode_manifest_bytes(manifest_bytes, manifest);
    return VERIFY_OK;
}

static uint8_t checked_u32_add(uint32_t left, uint32_t right, uint32_t *out)
{
    if (right > (UINT32_MAX - left)) {
        return 0U;
    }

    *out = left + right;
    return 1U;
}

static verify_status_t validate_manifest_header(
    const signed_manifest_t *m,
    const signed_image_validation_policy_t *policy
)
{
    if ((m == NULL) || (policy == NULL)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if (u32_equal_redundant(m->magic, SIGNED_IMAGE_MAGIC) == 0U) {
        return VERIFY_BAD_MAGIC;
    }

    if (u32_equal_redundant(m->header_version, policy->header_version) == 0U) {
        return VERIFY_BAD_HEADER_VERSION;
    }

    const uint32_t unsupported_flags =
        m->flags & ~(policy->allowed_flags_mask);

    if (unsupported_flags != 0UL) {
        return VERIFY_BAD_FLAGS;
    }

    if (policy->require_update_fields != 0U) {
        if (m->reserved0 != policy->target_compatibility) {
            return VERIFY_BAD_TARGET_COMPATIBILITY;
        }

        if (m->reserved1 != policy->image_type) {
            return VERIFY_BAD_IMAGE_TYPE;
        }
    } else {
        if ((m->reserved0 != 0UL) || (m->reserved1 != 0UL)) {
            return VERIFY_BAD_RESERVED;
        }
    }

    if (m->image_version < MIN_IMAGE_VERSION) {
        return VERIFY_ROLLBACK_VERSION;
    }

    return VERIFY_OK;
}

static verify_status_t validate_payload_range(
    const signed_manifest_t *m,
    const signed_image_validation_policy_t *policy,
    uint32_t *payload_end
)
{
    if ((m == NULL) || (policy == NULL) || (payload_end == NULL)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if (u32_equal_redundant(
            m->vector_address,
            policy->vector_address
        ) == 0U) {
        return VERIFY_BAD_VECTOR_ADDRESS;
    }

    if (m->image_size < APPLICATION_MIN_SIZE) {
        return VERIFY_BAD_SIZE;
    }

    if (checked_u32_add(m->vector_address, m->image_size, payload_end) == 0U) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if (m->image_size > policy->maximum_payload_size) {
        return VERIFY_BAD_SIZE;
    }

    if (*payload_end > policy->payload_flash_end) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    return VERIFY_OK;
}

static verify_status_t validate_payload_range_independent(
    const signed_manifest_t *m,
    const signed_image_validation_policy_t *policy,
    uint32_t payload_end
)
{
    if ((m == NULL) || (policy == NULL) ||
        (policy->payload_flash_end < policy->vector_address)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    const uint32_t application_capacity =
        policy->payload_flash_end - policy->vector_address;

    if (u32_equal_redundant(
            m->vector_address,
            policy->vector_address
        ) == 0U) {
        return VERIFY_BAD_VECTOR_ADDRESS;
    }

    if (m->image_size < APPLICATION_VECTOR_MIN_SIZE) {
        return VERIFY_BAD_SIZE;
    }

    if (m->image_size > application_capacity) {
        return VERIFY_BAD_SIZE;
    }

    if ((policy->payload_flash_end - m->vector_address) < m->image_size) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if ((payload_end < m->vector_address) ||
        (payload_end > policy->payload_flash_end)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if ((payload_end - m->vector_address) != m->image_size) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    return VERIFY_OK;
}

static verify_status_t validate_application_msp(uint32_t msp)
{
    if ((msp <= APPLICATION_MSP_BASE) ||
        (msp > APPLICATION_MSP_END) ||
        ((msp & (APPLICATION_MSP_ALIGNMENT - 1UL)) != 0UL)) {
        return VERIFY_BAD_STACK;
    }

    return VERIFY_OK;
}

static verify_status_t validate_application_reset(
    uint32_t reset_vector,
    uint32_t reset_address,
    uint32_t vector_address,
    uint32_t payload_end,
    uint32_t payload_flash_end
)
{
    if (((reset_vector & 1UL) == 0U) ||
        ((reset_vector & ~1UL) != reset_address) ||
        (reset_address < vector_address) ||
        (reset_address >= payload_end) ||
        (reset_address >= payload_flash_end)) {
        return VERIFY_BAD_RESET_VECTOR;
    }

    return VERIFY_OK;
}

static void read_application_vector(
    const uint8_t *payload,
    application_vector_t *vector
)
{
    vector->initial_msp =
        load_le32(&payload[APPLICATION_VECTOR_MSP_OFFSET]);
    vector->reset_vector =
        load_le32(&payload[APPLICATION_VECTOR_RESET_OFFSET]);
    vector->reset_address = vector->reset_vector & ~1UL;
}

static verify_status_t validate_vector_table(
    const signed_manifest_t *m,
    const signed_image_validation_policy_t *policy,
    const uint8_t *payload,
    uint32_t payload_end,
    signed_image_jump_context_t *context
)
{
    if ((m == NULL) || (policy == NULL) || (payload == NULL)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    application_vector_t vector;
    read_application_vector(payload, &vector);

    verify_status_t status = validate_application_msp(vector.initial_msp);

    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_application_reset(
        vector.reset_vector,
        vector.reset_address,
        m->vector_address,
        payload_end,
        policy->payload_flash_end
    );

    if (status != VERIFY_OK) {
        return status;
    }

    if (context != NULL) {
        context->vector_address = m->vector_address;
        context->image_size = m->image_size;
        context->payload_end = payload_end;
        context->initial_msp = vector.initial_msp;
        context->reset_vector = vector.reset_vector;
        context->reset_address = vector.reset_address;
    }

    return VERIFY_OK;
}

static verify_status_t slot_for_vector_address(
    uint32_t vector_address,
    const boot_slot_descriptor_t **slot
)
{
    const boot_slot_descriptor_t *candidate = NULL;

    if (slot == NULL) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    *slot = NULL;

    if (boot_slot_lookup((uint32_t)BOOT_SLOT_A, &candidate) ==
            BOOT_SLOT_LOOKUP_OK &&
        candidate->payload_base == vector_address) {
        *slot = candidate;
        return VERIFY_OK;
    }

    if (boot_slot_lookup((uint32_t)BOOT_SLOT_B, &candidate) ==
            BOOT_SLOT_LOOKUP_OK &&
        candidate->payload_base == vector_address) {
        *slot = candidate;
        return VERIFY_OK;
    }

    return VERIFY_BAD_VECTOR_ADDRESS;
}

static verify_status_t validate_jump_context(
    const signed_image_jump_context_t *context
)
{
    uint32_t recomputed_payload_end = 0U;
    const boot_slot_descriptor_t *slot = NULL;

    if (context == NULL) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    verify_status_t status =
        slot_for_vector_address(context->vector_address, &slot);
    if (status != VERIFY_OK) {
        return status;
    }

    if ((context->vector_address & (APPLICATION_VTOR_ALIGNMENT - 1UL)) != 0UL) {
        return VERIFY_BAD_VECTOR_ADDRESS;
    }

    if (context->image_size < APPLICATION_VECTOR_MIN_SIZE) {
        return VERIFY_BAD_SIZE;
    }

    if (context->image_size > slot->maximum_payload_size) {
        return VERIFY_BAD_SIZE;
    }

    if (checked_u32_add(
            context->vector_address,
            context->image_size,
            &recomputed_payload_end
        ) == 0U) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if (u32_equal_redundant(
            recomputed_payload_end,
            context->payload_end
        ) == 0U) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    if ((context->payload_end <= context->vector_address) ||
        (context->payload_end > slot->slot_end)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    status = validate_application_msp(context->initial_msp);

    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_application_reset(
        context->reset_vector,
        context->reset_address,
        context->vector_address,
        context->payload_end,
        slot->slot_end
    );

    return status;
}

static verify_status_t signed_image_verify_buffer_with_policy(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    const uint8_t signature_bytes[SIGNED_SIGNATURE_SIZE],
    const uint8_t *payload,
    size_t payload_capacity,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const signed_image_validation_policy_t *policy
)
{
    if ((manifest_bytes == NULL) ||
        (signature_bytes == NULL) ||
        (payload == NULL) ||
        (public_key == NULL) ||
        (policy == NULL)) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    signed_manifest_t decoded_manifest;
    decode_manifest_bytes(manifest_bytes, &decoded_manifest);

    const signed_manifest_t *m = &decoded_manifest;
    uint32_t payload_end = 0U;
    verify_status_t status = validate_manifest_header(m, policy);

    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_payload_range(m, policy, &payload_end);
    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_payload_range_independent(m, policy, payload_end);
    if (status != VERIFY_OK) {
        return status;
    }

    if ((size_t)m->image_size > payload_capacity) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    status = validate_vector_table(m, policy, payload, payload_end, NULL);
    if (status != VERIFY_OK) {
        return status;
    }

    uint8_t computed_hash[SIGNED_PAYLOAD_HASH_SIZE];
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

verify_status_t signed_image_verify_buffer(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    const uint8_t signature_bytes[SIGNED_SIGNATURE_SIZE],
    const uint8_t *payload,
    size_t payload_capacity,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE]
)
{
    const signed_image_validation_policy_t policy = {
        .header_version = SIGNED_HEADER_VERSION,
        .vector_address = APPLICATION_BASE_ADDRESS,
        .payload_flash_end = APPLICATION_FLASH_END,
        .maximum_payload_size = MAX_PAYLOAD_SIZE,
        .allowed_flags_mask = (uint32_t)SIGNED_IMAGE_FLAGS_ALLOWED_MASK,
        .target_compatibility = 0UL,
        .image_type = 0UL,
        .require_update_fields = 0U,
    };

    return signed_image_verify_buffer_with_policy(
        manifest_bytes,
        signature_bytes,
        payload,
        payload_capacity,
        public_key,
        &policy
    );
}

verify_status_t signed_image_verify_update_slot_buffer(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    const uint8_t signature_bytes[SIGNED_SIGNATURE_SIZE],
    const uint8_t *payload,
    size_t payload_capacity,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const boot_slot_descriptor_t *slot
)
{
    if ((slot == NULL) ||
        (slot->payload_base <= slot->signed_image_base) ||
        (slot->payload_base >= slot->slot_end) ||
        (slot->maximum_payload_size !=
            (slot->slot_end - slot->payload_base))) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    const signed_image_validation_policy_t policy = {
        .header_version = UPDATE_PACKAGE_FORMAT_VERSION,
        .vector_address = slot->payload_base,
        .payload_flash_end = slot->slot_end,
        .maximum_payload_size = slot->maximum_payload_size,
        .allowed_flags_mask = (uint32_t)SIGNED_IMAGE_FLAGS_ALLOWED_MASK,
        .target_compatibility = UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1,
        .image_type = UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION,
        .require_update_fields = 1U,
    };

    return signed_image_verify_buffer_with_policy(
        manifest_bytes,
        signature_bytes,
        payload,
        payload_capacity,
        public_key,
        &policy
    );
}

verify_status_t signed_image_prepare_update_slot_buffer(
    const uint8_t manifest_bytes[SIGNED_MANIFEST_SIZE],
    const uint8_t *payload,
    size_t payload_capacity,
    const boot_slot_descriptor_t *slot,
    signed_image_jump_context_t *context
)
{
    if ((manifest_bytes == NULL) ||
        (payload == NULL) ||
        (slot == NULL) ||
        (context == NULL) ||
        (slot->payload_base <= slot->signed_image_base) ||
        (slot->payload_base >= slot->slot_end) ||
        (slot->maximum_payload_size !=
            (slot->slot_end - slot->payload_base))) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    signed_manifest_t decoded_manifest;
    decode_manifest_bytes(manifest_bytes, &decoded_manifest);

    const signed_image_validation_policy_t policy = {
        .header_version = UPDATE_PACKAGE_FORMAT_VERSION,
        .vector_address = slot->payload_base,
        .payload_flash_end = slot->slot_end,
        .maximum_payload_size = slot->maximum_payload_size,
        .allowed_flags_mask = (uint32_t)SIGNED_IMAGE_FLAGS_ALLOWED_MASK,
        .target_compatibility = UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1,
        .image_type = UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION,
        .require_update_fields = 1U,
    };
    uint32_t payload_end = 0U;
    verify_status_t status =
        validate_manifest_header(&decoded_manifest, &policy);

    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_payload_range(&decoded_manifest, &policy, &payload_end);
    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_payload_range_independent(
        &decoded_manifest,
        &policy,
        payload_end
    );
    if (status != VERIFY_OK) {
        return status;
    }

    if ((size_t)decoded_manifest.image_size > payload_capacity) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    status = validate_vector_table(
        &decoded_manifest,
        &policy,
        payload,
        payload_end,
        context
    );
    if (status != VERIFY_OK) {
        return status;
    }

    return validate_jump_context(context);
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
    case VERIFY_BAD_TARGET_COMPATIBILITY:
        return "BAD TARGET COMPATIBILITY";
    case VERIFY_BAD_IMAGE_TYPE:     return "BAD IMAGE TYPE";
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

verify_status_t signed_image_prepare_jump(signed_image_jump_context_t *context)
{
    signed_manifest_t decoded_manifest;
    uint32_t payload_end = 0U;

    if (context == NULL) {
        return VERIFY_BAD_PAYLOAD_RANGE;
    }

    decode_manifest_bytes((const uint8_t *)SIGNED_IMAGE_BASE, &decoded_manifest);

    const signed_image_validation_policy_t policy = {
        .header_version = SIGNED_HEADER_VERSION,
        .vector_address = APPLICATION_BASE_ADDRESS,
        .payload_flash_end = APPLICATION_FLASH_END,
        .maximum_payload_size = MAX_PAYLOAD_SIZE,
        .allowed_flags_mask = (uint32_t)SIGNED_IMAGE_FLAGS_ALLOWED_MASK,
        .target_compatibility = 0UL,
        .image_type = 0UL,
        .require_update_fields = 0U,
    };

    verify_status_t status =
        validate_manifest_header(&decoded_manifest, &policy);

    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_payload_range(&decoded_manifest, &policy, &payload_end);
    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_payload_range_independent(
        &decoded_manifest,
        &policy,
        payload_end
    );
    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_vector_table(
        &decoded_manifest,
        &policy,
        (const uint8_t *)APPLICATION_BASE_ADDRESS,
        payload_end,
        context
    );
    if (status != VERIFY_OK) {
        return status;
    }

    return validate_jump_context(context);
}

static void data_sync_barrier(void)
{
    __asm volatile ("dsb" ::: "memory");
}

static void instruction_sync_barrier(void)
{
    __asm volatile ("isb" ::: "memory");
}

static verify_status_t confirm_jump_context_matches_flash(
    const signed_image_jump_context_t *context
)
{
    application_vector_t vector;
    const boot_slot_descriptor_t *slot = NULL;
    verify_status_t status =
        slot_for_vector_address(context->vector_address, &slot);

    if (status != VERIFY_OK) {
        return status;
    }

    read_application_vector((const uint8_t *)context->vector_address, &vector);

    if (u32_equal_redundant(
            vector.initial_msp,
            context->initial_msp
        ) == 0U) {
        return VERIFY_BAD_STACK;
    }

    if ((u32_equal_redundant(
             vector.reset_vector,
             context->reset_vector
         ) == 0U) ||
        (u32_equal_redundant(
             vector.reset_address,
             context->reset_address
         ) == 0U)) {
        return VERIFY_BAD_RESET_VECTOR;
    }

    status = validate_application_msp(vector.initial_msp);

    if (status != VERIFY_OK) {
        return status;
    }

    status = validate_application_reset(
        vector.reset_vector,
        vector.reset_address,
        context->vector_address,
        context->payload_end,
        slot->slot_end
    );

    return status;
}

verify_status_t signed_image_jump(const signed_image_jump_context_t *context)
{
    verify_status_t status = validate_jump_context(context);

    if (status != VERIFY_OK) {
        return status;
    }

    status = confirm_jump_context_matches_flash(context);

    if (status != VERIFY_OK) {
        return status;
    }

    const entry_fn_t entry = (entry_fn_t)(uintptr_t)context->reset_vector;

    __asm volatile ("cpsid i" ::: "memory");
    data_sync_barrier();
    instruction_sync_barrier();

    SYST_CSR = 0U;

    for (uint32_t i = 0U; i < 8U; ++i) {
        REG32(NVIC_ICER_BASE + (i * 4U)) = 0xFFFFFFFFUL;
        REG32(NVIC_ICPR_BASE + (i * 4U)) = 0xFFFFFFFFUL;
    }

    SCB_VTOR = context->vector_address;
    data_sync_barrier();
    instruction_sync_barrier();

    __asm volatile (
        "msr msp, %0\n"
        :
        : "r" (context->initial_msp)
        : "memory"
    );
    data_sync_barrier();
    instruction_sync_barrier();

    __asm volatile ("cpsie i" ::: "memory");

    entry();

    return VERIFY_BAD_RESET_VECTOR;
}
#else
verify_status_t signed_image_prepare_jump(signed_image_jump_context_t *context)
{
    (void)context;
    return VERIFY_BAD_PAYLOAD_RANGE;
}

verify_status_t signed_image_jump(const signed_image_jump_context_t *context)
{
    return validate_jump_context(context);
}
#endif
