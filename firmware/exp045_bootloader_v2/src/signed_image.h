#ifndef SIGNED_IMAGE_H
#define SIGNED_IMAGE_H

#include <stdint.h>

#include "flash_layout.h"

#define SIGNED_IMAGE_MAGIC     0x31474953UL
#define SIGNED_HEADER_VERSION  1UL
#define MIN_IMAGE_VERSION      2UL

typedef struct {
    uint32_t magic;
    uint32_t header_version;
    uint32_t image_version;
    uint32_t vector_address;
    uint32_t image_size;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t payload_sha512[64];
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
    VERIFY_BAD_SIGNATURE
} verify_status_t;

verify_status_t signed_image_verify(void);
const char *signed_image_status_text(verify_status_t status);
void signed_image_jump(void);

#endif
