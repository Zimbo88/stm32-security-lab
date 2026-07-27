#include "boot_policy.h"

#include "boot_flash_target.h"
#include "firmware_public_key.h"

verify_status_t boot_policy_verify(void)
{
    return signed_image_verify();
}

static boot_slot_selection_status_t verify_target_slot(
    void *context,
    const boot_slot_descriptor_t *slot,
    signed_image_jump_context_t *jump_context,
    verify_status_t *verify_status
)
{
    (void)context;

    if ((slot == NULL) || (jump_context == NULL) || (verify_status == NULL)) {
        return BOOT_SLOT_SELECTION_ERR_INVALID_ARGUMENT;
    }

    *verify_status = signed_image_verify_update_slot_buffer(
        (const uint8_t *)(uintptr_t)slot->manifest_address,
        (const uint8_t *)(uintptr_t)slot->signature_address,
        (const uint8_t *)(uintptr_t)slot->payload_base,
        (size_t)slot->maximum_payload_size,
        firmware_public_key,
        slot
    );
    if (*verify_status != VERIFY_OK) {
        return BOOT_SLOT_SELECTION_ERR_VERIFY;
    }

    *verify_status = signed_image_prepare_update_slot_buffer(
        (const uint8_t *)(uintptr_t)slot->manifest_address,
        (const uint8_t *)(uintptr_t)slot->payload_base,
        (size_t)slot->maximum_payload_size,
        slot,
        jump_context
    );

    return (*verify_status == VERIFY_OK)
        ? BOOT_SLOT_SELECTION_OK
        : BOOT_SLOT_SELECTION_ERR_VERIFY;
}

boot_slot_selection_status_t boot_policy_select(
    boot_slot_selection_result_t *result
)
{
    boot_flash_t flash;

    if (boot_flash_target_init_metadata(&flash) != BOOT_FLASH_OK) {
        return BOOT_SLOT_SELECTION_ERR_METADATA;
    }

    const boot_slot_selection_options_t options = {
        .metadata_flash = &flash,
        .verify_slot = verify_target_slot,
        .verify_context = NULL,
        .fault_hook = NULL,
        .fault_context = NULL,
    };

    return boot_slot_selection_select(&options, result);
}
