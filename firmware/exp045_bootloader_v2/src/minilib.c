#include <stddef.h>
#include <stdint.h>

void *memset(void *destination, int value, size_t count)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t byte = (uint8_t)value;

    while (count != 0U) {
        *dst++ = byte;
        --count;
    }

    return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;

    while (count != 0U) {
        *dst++ = *src++;
        --count;
    }

    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;

    if ((dst == src) || (count == 0U)) {
        return destination;
    }

    if (dst < src) {
        while (count != 0U) {
            *dst++ = *src++;
            --count;
        }
    } else {
        dst += count;
        src += count;

        while (count != 0U) {
            *--dst = *--src;
            --count;
        }
    }

    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;

    while (count != 0U) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }

        ++a;
        ++b;
        --count;
    }

    return 0;
}
