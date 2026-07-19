#include "boot_slot.h"

#include <stddef.h>

#include "stm32f429_memory_layout.h"

_Static_assert(
    STM32F429_SLOT_A_ID == (uint32_t)BOOT_SLOT_A,
    "Slot A ID must match generated layout"
);
_Static_assert(
    STM32F429_SLOT_B_ID == (uint32_t)BOOT_SLOT_B,
    "Slot B ID must match generated layout"
);
_Static_assert(
    STM32F429_SLOT_A_MANIFEST_BASE == STM32F429_SLOT_A_SIGNED_IMAGE_BASE,
    "Slot A manifest must start at signed-image base"
);
_Static_assert(
    STM32F429_SLOT_B_MANIFEST_BASE == STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
    "Slot B manifest must start at signed-image base"
);
_Static_assert(
    STM32F429_SLOT_A_SIGNATURE_BASE ==
        (STM32F429_SLOT_A_MANIFEST_BASE + STM32F429_SIGNED_MANIFEST_SIZE),
    "Slot A signature must follow manifest"
);
_Static_assert(
    STM32F429_SLOT_B_SIGNATURE_BASE ==
        (STM32F429_SLOT_B_MANIFEST_BASE + STM32F429_SIGNED_MANIFEST_SIZE),
    "Slot B signature must follow manifest"
);
_Static_assert(
    STM32F429_SLOT_A_PAYLOAD_BASE ==
        (STM32F429_SLOT_A_SIGNED_IMAGE_BASE +
         STM32F429_SIGNED_IMAGE_HEADER_SIZE),
    "Slot A payload must follow signed-image header"
);
_Static_assert(
    STM32F429_SLOT_B_PAYLOAD_BASE ==
        (STM32F429_SLOT_B_SIGNED_IMAGE_BASE +
         STM32F429_SIGNED_IMAGE_HEADER_SIZE),
    "Slot B payload must follow signed-image header"
);
_Static_assert(
    STM32F429_SLOT_A_SIZE == STM32F429_SLOT_B_SIZE,
    "Slot A and Slot B must have equal capacity"
);
_Static_assert(
    STM32F429_SLOT_A_PAYLOAD_MAX_SIZE == STM32F429_SLOT_B_PAYLOAD_MAX_SIZE,
    "Slot A and Slot B must have equal payload capacity"
);
_Static_assert(
    STM32F429_SIGNED_IMAGE_HEADER_SIZE == 0x200UL,
    "signed-image header size must remain 0x200 bytes"
);
_Static_assert(
    STM32F429_SLOT_A_PAYLOAD_MAX_SIZE ==
        (STM32F429_SLOT_A_END - STM32F429_SLOT_A_PAYLOAD_BASE),
    "Slot A payload capacity must match its slot end"
);
_Static_assert(
    STM32F429_SLOT_B_PAYLOAD_MAX_SIZE ==
        (STM32F429_SLOT_B_END - STM32F429_SLOT_B_PAYLOAD_BASE),
    "Slot B payload capacity must match its slot end"
);
_Static_assert(
    STM32F429_SLOT_A_SIGNED_IMAGE_BASE >= STM32F429_UPDATE_METADATA_END,
    "Slot A must not overlap update metadata"
);
_Static_assert(
    STM32F429_SLOT_A_END <= STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
    "Slot A must not overlap Slot B"
);
_Static_assert(
    STM32F429_SLOT_B_END <= STM32F429_RECOVERY_BASE,
    "Slot B must not overlap recovery"
);
_Static_assert(
    STM32F429_RECOVERY_END == STM32F429_FLASH_END,
    "recovery must end exactly at physical flash end"
);
_Static_assert(
    STM32F429_SLOT_A_LAST_SECTOR <= 11UL &&
        STM32F429_SLOT_B_LAST_SECTOR <= 11UL &&
        STM32F429_RECOVERY_LAST_SECTOR <= 11UL,
    "STM32F429IGT6 1 MiB profile cannot reference sectors above 11"
);
_Static_assert(
    (STM32F429_SLOT_A_PAYLOAD_BASE & 0xFFUL) == 0UL,
    "Slot A payload base must be VTOR-aligned"
);
_Static_assert(
    (STM32F429_SLOT_B_PAYLOAD_BASE & 0xFFUL) == 0UL,
    "Slot B payload base must be VTOR-aligned"
);

static const boot_slot_descriptor_t boot_slots[] = {
    {
        .id = BOOT_SLOT_A,
        .signed_image_base = STM32F429_SLOT_A_SIGNED_IMAGE_BASE,
        .manifest_address = STM32F429_SLOT_A_MANIFEST_BASE,
        .signature_address = STM32F429_SLOT_A_SIGNATURE_BASE,
        .payload_base = STM32F429_SLOT_A_PAYLOAD_BASE,
        .slot_end = STM32F429_SLOT_A_END,
        .maximum_payload_size = STM32F429_SLOT_A_PAYLOAD_MAX_SIZE,
        .first_sector = STM32F429_SLOT_A_FIRST_SECTOR,
        .last_sector = STM32F429_SLOT_A_LAST_SECTOR,
    },
    {
        .id = BOOT_SLOT_B,
        .signed_image_base = STM32F429_SLOT_B_SIGNED_IMAGE_BASE,
        .manifest_address = STM32F429_SLOT_B_MANIFEST_BASE,
        .signature_address = STM32F429_SLOT_B_SIGNATURE_BASE,
        .payload_base = STM32F429_SLOT_B_PAYLOAD_BASE,
        .slot_end = STM32F429_SLOT_B_END,
        .maximum_payload_size = STM32F429_SLOT_B_PAYLOAD_MAX_SIZE,
        .first_sector = STM32F429_SLOT_B_FIRST_SECTOR,
        .last_sector = STM32F429_SLOT_B_LAST_SECTOR,
    },
};

_Static_assert(
    sizeof(boot_slots) / sizeof(boot_slots[0]) == 2U,
    "exactly two boot slots must be described"
);

boot_slot_lookup_status_t boot_slot_lookup(
    uint32_t slot_id,
    const boot_slot_descriptor_t **descriptor
)
{
    if (descriptor == NULL) {
        return BOOT_SLOT_LOOKUP_INVALID;
    }

    *descriptor = NULL;

    if (slot_id >= (sizeof(boot_slots) / sizeof(boot_slots[0]))) {
        return BOOT_SLOT_LOOKUP_INVALID;
    }

    if ((uint32_t)boot_slots[slot_id].id != slot_id) {
        return BOOT_SLOT_LOOKUP_INVALID;
    }

    *descriptor = &boot_slots[slot_id];
    return BOOT_SLOT_LOOKUP_OK;
}

const boot_slot_descriptor_t *boot_slot_default(void)
{
    return &boot_slots[(uint32_t)BOOT_SLOT_A];
}
