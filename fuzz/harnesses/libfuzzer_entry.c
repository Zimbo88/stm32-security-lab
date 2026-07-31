#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_metadata.h"
#include "boot_slot.h"
#include "update_package.h"
#include "update_protocol.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t copy_a[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t copy_b[STM32F429_BOOT_METADATA_RECORD_SIZE];
    uint8_t key[FIRMWARE_PUBLIC_KEY_SIZE];
    update_protocol_parser_t parser;
    update_package_t package;
    update_package_header_t header;
    verify_status_t verify_status;
    const boot_slot_descriptor_t *slot = NULL;

    if ((data == NULL) || (size == 0U)) {
        return 0;
    }

    switch (data[0] & 3U) {
    case 0U:
        update_protocol_parser_init(&parser);
        for (size_t i = 1U; i < size; ++i) {
            (void)update_protocol_parser_push(&parser, data[i]);
        }
        break;
    case 1U:
        memset(copy_a, 0xFF, sizeof(copy_a));
        memset(copy_b, 0xFF, sizeof(copy_b));
        memcpy(copy_a, &data[1],
            (size - 1U > sizeof(copy_a)) ? sizeof(copy_a) : size - 1U);
        if (size > (sizeof(copy_a) + 1U)) {
            const size_t available = size - sizeof(copy_a) - 1U;
            memcpy(copy_b, &data[sizeof(copy_a) + 1U],
                (available > sizeof(copy_b)) ? sizeof(copy_b) : available);
        }
        (void)boot_metadata_recover(
            copy_a, copy_b, sizeof(copy_a), NULL, NULL
        );
        break;
    case 2U:
        memset(key, 0x5A, sizeof(key));
        (void)update_package_parse(&data[1], size - 1U, &package);
        (void)boot_slot_lookup(data[1] & 1U, &slot);
        if ((slot != NULL) && (size - 1U >= SIGNED_IMAGE_HEADER_SIZE)) {
            (void)update_package_verify_header_for_slot(
                &data[1], SIGNED_IMAGE_HEADER_SIZE, key, slot,
                &header, &verify_status
            );
        }
        break;
    default:
        (void)update_package_parse(data, size, &package);
        break;
    }
    return 0;
}
