#include "image.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define SCB_VTOR   REG32(0xE000ED08UL)
#define SYST_CSR   REG32(0xE000E010UL)
#define NVIC_ICER_BASE 0xE000E180UL
#define NVIC_ICPR_BASE 0xE000E280UL

typedef void (*entry_fn_t)(void);

static const image_header_t *header(void)
{
    return (const image_header_t *)IMAGE_HEADER_ADDRESS;
}

static uint32_t crc32_ieee(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < length; ++i) {
        crc ^= data[i];

        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

image_status_t image_validate(uint32_t *computed_crc)
{
    const image_header_t *h = header();

    if (h->magic != IMAGE_MAGIC) {
        return IMAGE_BAD_MAGIC;
    }

    if (h->header_version != IMAGE_HEADER_VERSION) {
        return IMAGE_BAD_HEADER_VERSION;
    }

    if (h->vector_address != IMAGE_VECTOR_ADDRESS) {
        return IMAGE_BAD_VECTOR_ADDRESS;
    }

    if ((h->image_size < 8U) || (h->image_size > IMAGE_MAX_SIZE)) {
        return IMAGE_BAD_SIZE;
    }

    const uint32_t msp = *(volatile const uint32_t *)(h->vector_address + 0U);
    const uint32_t reset = *(volatile const uint32_t *)(h->vector_address + 4U);

    if ((msp < 0x20000000UL) || (msp > 0x20020000UL)) {
        return IMAGE_BAD_STACK;
    }

    if (((reset & 1UL) == 0U) ||
        ((reset & ~1UL) < h->vector_address) ||
        ((reset & ~1UL) >= (h->vector_address + h->image_size))) {
        return IMAGE_BAD_RESET_VECTOR;
    }

    const uint32_t actual =
        crc32_ieee((const uint8_t *)h->vector_address, h->image_size);

    if (computed_crc != (uint32_t *)0) {
        *computed_crc = actual;
    }

    if (actual != h->image_crc32) {
        return IMAGE_BAD_CRC;
    }

    return IMAGE_OK;
}

const char *image_status_text(image_status_t status)
{
    switch (status) {
    case IMAGE_OK:                 return "OK";
    case IMAGE_BAD_MAGIC:          return "BAD MAGIC";
    case IMAGE_BAD_HEADER_VERSION: return "BAD HEADER VERSION";
    case IMAGE_BAD_VECTOR_ADDRESS: return "BAD VECTOR ADDRESS";
    case IMAGE_BAD_SIZE:           return "BAD IMAGE SIZE";
    case IMAGE_BAD_STACK:          return "BAD INITIAL MSP";
    case IMAGE_BAD_RESET_VECTOR:   return "BAD RESET VECTOR";
    case IMAGE_BAD_CRC:            return "CRC MISMATCH";
    default:                       return "UNKNOWN";
    }
}

void image_jump(void)
{
    const image_header_t *h = header();
    const uint32_t new_msp =
        *(volatile const uint32_t *)(h->vector_address + 0U);
    const uint32_t reset =
        *(volatile const uint32_t *)(h->vector_address + 4U);
    const entry_fn_t entry = (entry_fn_t)reset;

    __asm volatile ("cpsid i" ::: "memory");

    SYST_CSR = 0U;

    for (uint32_t i = 0U; i < 8U; ++i) {
        REG32(NVIC_ICER_BASE + (i * 4U)) = 0xFFFFFFFFUL;
        REG32(NVIC_ICPR_BASE + (i * 4U)) = 0xFFFFFFFFUL;
    }

    SCB_VTOR = h->vector_address;

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
