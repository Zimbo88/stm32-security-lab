#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

#define IMAGE_HEADER_ADDRESS 0x08008000UL
#define IMAGE_VECTOR_ADDRESS 0x08008200UL
#define IMAGE_MAGIC          0x31474D49UL
#define IMAGE_HEADER_VERSION 1UL
#define IMAGE_MAX_SIZE       (0x08100000UL - IMAGE_VECTOR_ADDRESS)

typedef struct {
    uint32_t magic;
    uint32_t header_version;
    uint32_t image_version;
    uint32_t vector_address;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t flags;
    uint32_t reserved;
} image_header_t;

typedef enum {
    IMAGE_OK = 0,
    IMAGE_BAD_MAGIC,
    IMAGE_BAD_HEADER_VERSION,
    IMAGE_BAD_VECTOR_ADDRESS,
    IMAGE_BAD_SIZE,
    IMAGE_BAD_STACK,
    IMAGE_BAD_RESET_VECTOR,
    IMAGE_BAD_CRC
} image_status_t;

image_status_t image_validate(uint32_t *computed_crc);
const char *image_status_text(image_status_t status);
void image_jump(void);

#endif
