#ifndef BOOT_SLOT_H
#define BOOT_SLOT_H

#include <stdint.h>

#define BOOT_SLOT_NONE 0xFFFFFFFFUL

typedef enum {
    BOOT_SLOT_A = 0,
    BOOT_SLOT_B = 1
} boot_slot_id_t;

typedef enum {
    BOOT_SLOT_LOOKUP_OK = 0,
    BOOT_SLOT_LOOKUP_INVALID = 1
} boot_slot_lookup_status_t;

typedef struct {
    boot_slot_id_t id;
    uint32_t signed_image_base;
    uint32_t manifest_address;
    uint32_t signature_address;
    uint32_t payload_base;
    uint32_t slot_end;
    uint32_t maximum_payload_size;
    uint32_t first_sector;
    uint32_t last_sector;
} boot_slot_descriptor_t;

boot_slot_lookup_status_t boot_slot_lookup(
    uint32_t slot_id,
    const boot_slot_descriptor_t **descriptor
);
const boot_slot_descriptor_t *boot_slot_default(void);

#endif
