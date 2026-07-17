#include <stddef.h>
#include <stdint.h>

#include "firmware_public_key.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "signed_image.h"
#include "board.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define SCB_VTOR       REG32(0xE000ED08UL)
#define SYST_CSR       REG32(0xE000E010UL)
#define NVIC_ICER_BASE 0xE000E180UL
#define NVIC_ICPR_BASE 0xE000E280UL

typedef void (*entry_fn_t)(void);

static const signed_manifest_t *manifest(void)
{
    return (const signed_manifest_t *)SIGNED_IMAGE_BASE;
}

static const uint8_t *signature(void)
{
    return (const uint8_t *)SIGNATURE_ADDRESS;
}

verify_status_t signed_image_verify(void)
{
    const signed_manifest_t *m = manifest();

    if (sizeof(signed_manifest_t) != SIGNED_MANIFEST_SIZE) {
        return VERIFY_BAD_HEADER_VERSION;
    }

    if (m->magic != SIGNED_IMAGE_MAGIC) {
        return VERIFY_BAD_MAGIC;
    }

    if (m->header_version != SIGNED_HEADER_VERSION) {
        return VERIFY_BAD_HEADER_VERSION;
    }

    if (m->image_version < MIN_IMAGE_VERSION) {
        return VERIFY_ROLLBACK_VERSION;
    }

    if (m->vector_address != APPLICATION_BASE_ADDRESS) {
        return VERIFY_BAD_VECTOR_ADDRESS;
    }

    if ((m->image_size < 8U) || (m->image_size > MAX_PAYLOAD_SIZE)) {
        return VERIFY_BAD_SIZE;
    }

    const uint32_t msp =
        *(volatile const uint32_t *)(m->vector_address + 0U);
    const uint32_t reset =
        *(volatile const uint32_t *)(m->vector_address + 4U);

    if ((msp < BOARD_SRAM_BASE) || (msp > (BOARD_SRAM_BASE + BOARD_SRAM_SIZE))) {
        return VERIFY_BAD_STACK;
    }

    if (((reset & 1UL) == 0U) ||
        ((reset & ~1UL) < m->vector_address) ||
        ((reset & ~1UL) >= (m->vector_address + m->image_size))) {
        return VERIFY_BAD_RESET_VECTOR;
    }

    uint8_t computed_hash[64];
    crypto_sha512(
        computed_hash,
        (const uint8_t *)m->vector_address,
        (size_t)m->image_size
    );

    if (crypto_verify64(computed_hash, m->payload_sha512) != 0) {
        crypto_wipe(computed_hash, sizeof(computed_hash));
        return VERIFY_BAD_PAYLOAD_HASH;
    }

    crypto_wipe(computed_hash, sizeof(computed_hash));

    if (crypto_ed25519_check(
            signature(),
            firmware_public_key,
            (const uint8_t *)SIGNED_IMAGE_BASE,
            (size_t)SIGNED_MANIFEST_SIZE) != 0) {
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
    default:                        return "UNKNOWN";
    }
}

void signed_image_jump(void)
{
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
}
