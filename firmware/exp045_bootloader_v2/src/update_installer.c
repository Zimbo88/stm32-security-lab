#include "update_installer.h"

#include <stdint.h>
#include <string.h>

#include "flash_layout.h"
#include "image_policy.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "stm32f429_memory_layout.h"

static uint8_t checked_u32_add(uint32_t left, uint32_t right, uint32_t *out)
{
    if ((out == NULL) || (right > (UINT32_MAX - left))) {
        return 0U;
    }

    *out = left + right;
    return 1U;
}

static uint8_t checked_size_add(size_t left, size_t right, size_t *out)
{
    if ((out == NULL) || (right > (SIZE_MAX - left))) {
        return 0U;
    }

    *out = left + right;
    return 1U;
}

static uint8_t checked_round_up(size_t value, size_t alignment, size_t *out)
{
    size_t rounded = 0U;

    if ((out == NULL) || (alignment == 0U)) {
        return 0U;
    }

    const size_t remainder = value & (alignment - 1U);
    if (remainder == 0U) {
        *out = value;
        return 1U;
    }

    if (checked_size_add(value, alignment - remainder, &rounded) == 0U) {
        return 0U;
    }

    *out = rounded;
    return 1U;
}

static uint8_t options_are_valid(
    const boot_flash_t *flash,
    const update_install_options_t *options
)
{
    if ((flash == NULL) ||
        (options == NULL) ||
        (options->program_buffer == NULL) ||
        (options->readback_buffer == NULL) ||
        (options->installed_image_buffer == NULL) ||
        (options->program_buffer_size == 0U) ||
        (options->readback_buffer_size < options->program_buffer_size) ||
        (options->program_buffer_size > UINT32_MAX) ||
        (flash->program_alignment == 0UL) ||
        ((flash->program_alignment &
          (flash->program_alignment - 1UL)) != 0UL) ||
        ((options->program_buffer_size &
          (size_t)(flash->program_alignment - 1UL)) != 0U)) {
        return 0U;
    }

    return 1U;
}

static update_install_status_t maybe_fault(
    const update_install_options_t *options,
    update_install_fault_point_t point,
    uint32_t detail
)
{
    if ((options != NULL) && (options->fault_hook != NULL)) {
        return options->fault_hook(options->fault_context, point, detail);
    }

    return UPDATE_INSTALL_OK;
}

static update_install_status_t select_inactive_slot(
    const boot_metadata_record_t *metadata,
    const boot_slot_descriptor_t **active,
    const boot_slot_descriptor_t **candidate
)
{
    const boot_slot_descriptor_t *active_slot = NULL;
    const boot_slot_descriptor_t *candidate_slot = NULL;
    uint32_t candidate_slot_id = BOOT_SLOT_NONE;

    if ((metadata == NULL) || (active == NULL) || (candidate == NULL)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    *active = NULL;
    *candidate = NULL;

    if ((metadata->state != BOOT_METADATA_STATE_CONFIRMED) ||
        (metadata->candidate_slot != BOOT_SLOT_NONE) ||
        (metadata->confirmation_state != 1UL)) {
        return UPDATE_INSTALL_ERR_ACTIVE_STATE;
    }

    if (boot_slot_lookup(metadata->active_slot, &active_slot) !=
        BOOT_SLOT_LOOKUP_OK) {
        return UPDATE_INSTALL_ERR_SLOT;
    }

    candidate_slot_id = (metadata->active_slot == (uint32_t)BOOT_SLOT_A)
        ? (uint32_t)BOOT_SLOT_B
        : (uint32_t)BOOT_SLOT_A;

    if (boot_slot_lookup(candidate_slot_id, &candidate_slot) !=
        BOOT_SLOT_LOOKUP_OK) {
        return UPDATE_INSTALL_ERR_SLOT;
    }

    *active = active_slot;
    *candidate = candidate_slot;
    return UPDATE_INSTALL_OK;
}

static update_install_status_t init_restricted_flash(
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *candidate,
    boot_flash_t *restricted,
    boot_flash_region_t write_regions[3]
)
{
    if ((flash == NULL) ||
        (flash->ops == NULL) ||
        (candidate == NULL) ||
        (restricted == NULL) ||
        (write_regions == NULL)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    write_regions[0].base = STM32F429_BOOT_METADATA_A_BASE;
    write_regions[0].end = STM32F429_BOOT_METADATA_A_END;
    write_regions[1].base = STM32F429_BOOT_METADATA_B_BASE;
    write_regions[1].end = STM32F429_BOOT_METADATA_B_END;
    write_regions[2].base = candidate->signed_image_base;
    write_regions[2].end = candidate->slot_end;

    return (boot_flash_init(
                restricted,
                flash->context,
                flash->ops,
                write_regions,
                3U,
                flash->program_alignment
            ) == BOOT_FLASH_OK)
        ? UPDATE_INSTALL_OK
        : UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
}

static update_install_status_t commit_metadata_state(
    const boot_flash_t *flash,
    const boot_metadata_record_t *current,
    boot_metadata_state_t state,
    uint32_t active_slot,
    uint32_t candidate_slot,
    uint32_t image_version,
    const update_install_options_t *options,
    update_install_fault_point_t fault_point
)
{
    boot_metadata_record_t next;
    boot_metadata_status_t metadata_status = boot_metadata_prepare_next(
        current,
        state,
        active_slot,
        candidate_slot,
        image_version,
        0U,
        0U,
        0U,
        &next
    );

    if (metadata_status != BOOT_METADATA_OK) {
        return UPDATE_INSTALL_ERR_METADATA;
    }

    update_install_status_t fault = maybe_fault(options, fault_point, image_version);
    if (fault != UPDATE_INSTALL_OK) {
        return fault;
    }

    metadata_status = boot_metadata_commit(flash, &next);
    return (metadata_status == BOOT_METADATA_OK)
        ? UPDATE_INSTALL_OK
        : UPDATE_INSTALL_ERR_METADATA;
}

static update_install_status_t erase_candidate_slot(
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *candidate,
    const update_install_options_t *options,
    update_install_result_t *result
)
{
    for (uint32_t sector = candidate->first_sector;
         sector <= candidate->last_sector;
         ++sector) {
        update_install_status_t fault =
            maybe_fault(options, UPDATE_INSTALL_FAULT_ERASE_SECTOR, sector);
        if (fault != UPDATE_INSTALL_OK) {
            return fault;
        }

        if (boot_flash_erase_sector(flash, sector) != BOOT_FLASH_OK) {
            return UPDATE_INSTALL_ERR_ERASE;
        }

        if (result != NULL) {
            result->erased_sector_count += 1UL;
        }
    }

    return UPDATE_INSTALL_OK;
}

static update_install_status_t program_candidate_slot(
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *candidate,
    const update_package_t *package,
    const update_install_options_t *options,
    update_install_result_t *result
)
{
    size_t aligned_size = 0U;
    size_t offset = 0U;
    uint32_t slot_program_end = 0U;
    uint32_t block_index = 0U;

    if (checked_round_up(
            package->package_size,
            (size_t)flash->program_alignment,
            &aligned_size
        ) == 0U) {
        return UPDATE_INSTALL_ERR_CAPACITY;
    }

    if ((aligned_size > UINT32_MAX) ||
        (checked_u32_add(
            candidate->signed_image_base,
            (uint32_t)aligned_size,
            &slot_program_end
        ) == 0U) ||
        (slot_program_end > candidate->slot_end)) {
        return UPDATE_INSTALL_ERR_CAPACITY;
    }

    while (offset < aligned_size) {
        size_t chunk = aligned_size - offset;
        uint32_t address = 0U;

        if (chunk > options->program_buffer_size) {
            chunk = options->program_buffer_size;
        }

        memset(options->program_buffer, 0xFF, chunk);
        if (offset < package->package_size) {
            size_t available = package->package_size - offset;
            if (available > chunk) {
                available = chunk;
            }
            memcpy(
                options->program_buffer,
                &package->package_bytes[offset],
                available
            );
        }

        if (checked_u32_add(candidate->signed_image_base, (uint32_t)offset, &address) ==
            0U) {
            return UPDATE_INSTALL_ERR_CAPACITY;
        }

        update_install_status_t fault =
            maybe_fault(options, UPDATE_INSTALL_FAULT_PROGRAM_BLOCK, block_index);
        if (fault != UPDATE_INSTALL_OK) {
            return fault;
        }

        if (boot_flash_program_aligned(
                flash,
                address,
                options->program_buffer,
                chunk
            ) != BOOT_FLASH_OK) {
            return UPDATE_INSTALL_ERR_PROGRAM;
        }

        fault = maybe_fault(options, UPDATE_INSTALL_FAULT_READBACK, block_index);
        if (fault != UPDATE_INSTALL_OK) {
            return fault;
        }

        if (boot_flash_read(
                flash,
                address,
                options->readback_buffer,
                chunk
            ) != BOOT_FLASH_OK) {
            return UPDATE_INSTALL_ERR_READBACK;
        }

        if (memcmp(
                options->readback_buffer,
                options->program_buffer,
                chunk
            ) != 0) {
            return UPDATE_INSTALL_ERR_READBACK;
        }

        offset += chunk;
        block_index += 1UL;
        if (result != NULL) {
            result->programmed_block_count += 1UL;
        }
    }

    return UPDATE_INSTALL_OK;
}

static update_install_status_t hash_installed_payload(
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *candidate,
    const update_package_t *package,
    const update_install_options_t *options
)
{
    crypto_sha512_ctx ctx;
    uint8_t computed[SIGNED_PAYLOAD_HASH_SIZE];
    size_t offset = 0U;

    update_install_status_t fault =
        maybe_fault(options, UPDATE_INSTALL_FAULT_HASH_BEGIN, package->manifest.image_version);
    if (fault != UPDATE_INSTALL_OK) {
        return fault;
    }

    crypto_sha512_init(&ctx);

    while (offset < package->payload_size) {
        size_t chunk = package->payload_size - offset;
        uint32_t address = 0U;

        if (chunk > options->readback_buffer_size) {
            chunk = options->readback_buffer_size;
        }

        if (checked_u32_add(candidate->payload_base, (uint32_t)offset, &address) ==
            0U) {
            return UPDATE_INSTALL_ERR_CAPACITY;
        }

        if (boot_flash_read(
                flash,
                address,
                options->readback_buffer,
                chunk
            ) != BOOT_FLASH_OK) {
            return UPDATE_INSTALL_ERR_READBACK;
        }

        crypto_sha512_update(&ctx, options->readback_buffer, chunk);
        offset += chunk;
    }

    crypto_sha512_final(&ctx, computed);

    fault =
        maybe_fault(options, UPDATE_INSTALL_FAULT_HASH_COMPLETE, package->manifest.image_version);
    if (fault != UPDATE_INSTALL_OK) {
        crypto_wipe(computed, sizeof(computed));
        return fault;
    }

    if (crypto_verify64(computed, package->manifest.payload_sha512) != 0) {
        crypto_wipe(computed, sizeof(computed));
        return UPDATE_INSTALL_ERR_HASH;
    }

    crypto_wipe(computed, sizeof(computed));
    return UPDATE_INSTALL_OK;
}

static update_install_status_t verify_installed_package(
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *candidate,
    const update_package_t *package,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
)
{
    if (options->installed_image_buffer_size < package->package_size) {
        return UPDATE_INSTALL_ERR_CAPACITY;
    }

    if (boot_flash_read(
            flash,
            candidate->signed_image_base,
            options->installed_image_buffer,
            package->package_size
        ) != BOOT_FLASH_OK) {
        return UPDATE_INSTALL_ERR_READBACK;
    }

    update_install_status_t fault =
        maybe_fault(options, UPDATE_INSTALL_FAULT_VERIFY_INSTALLED, package->manifest.image_version);
    if (fault != UPDATE_INSTALL_OK) {
        return fault;
    }

    update_package_t installed;
    verify_status_t verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    update_package_status_t package_status = update_package_verify_for_slot(
        options->installed_image_buffer,
        package->package_size,
        public_key,
        candidate,
        &installed,
        &verify_status
    );

    if (result != NULL) {
        result->installed_verify_status = verify_status;
    }

    return (package_status == UPDATE_PACKAGE_OK)
        ? UPDATE_INSTALL_OK
        : UPDATE_INSTALL_ERR_INSTALLED_VERIFY;
}

update_install_status_t update_installer_install(
    const boot_flash_t *flash,
    const uint8_t *package_bytes,
    size_t package_size,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
)
{
    boot_metadata_record_t current;
    boot_metadata_record_t writing;
    boot_metadata_recovery_t recovery;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    boot_flash_t restricted;
    boot_flash_region_t write_regions[3];
    update_package_t package;
    verify_status_t package_verify_status = VERIFY_BAD_PAYLOAD_RANGE;

    if ((flash == NULL) ||
        (package_bytes == NULL) ||
        (public_key == NULL) ||
        (options_are_valid(flash, options) == 0U)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
        result->active_slot = BOOT_SLOT_NONE;
        result->candidate_slot = BOOT_SLOT_NONE;
        result->package_verify_status = VERIFY_BAD_PAYLOAD_RANGE;
        result->installed_verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    }

    boot_metadata_status_t metadata_status =
        boot_metadata_recover_from_flash(flash, &current, &recovery);
    if (metadata_status != BOOT_METADATA_OK) {
        return UPDATE_INSTALL_ERR_METADATA;
    }

    update_install_status_t status =
        select_inactive_slot(&current, &active, &candidate);
    if (status != UPDATE_INSTALL_OK) {
        return status;
    }

    if (result != NULL) {
        result->active_slot = (uint32_t)active->id;
        result->candidate_slot = (uint32_t)candidate->id;
        result->metadata_recovery = recovery;
    }

    update_package_status_t package_status = update_package_verify_for_slot(
        package_bytes,
        package_size,
        public_key,
        candidate,
        &package,
        &package_verify_status
    );

    if (result != NULL) {
        result->package_verify_status = package_verify_status;
    }

    if (package_status != UPDATE_PACKAGE_OK) {
        return UPDATE_INSTALL_ERR_PACKAGE;
    }

    if (package.manifest.image_version <= current.candidate_image_version) {
        return UPDATE_INSTALL_ERR_ROLLBACK;
    }

    if (result != NULL) {
        result->image_version = package.manifest.image_version;
    }

    status = init_restricted_flash(flash, candidate, &restricted, write_regions);
    if (status != UPDATE_INSTALL_OK) {
        return status;
    }

    status = commit_metadata_state(
        &restricted,
        &current,
        BOOT_METADATA_STATE_WRITING,
        (uint32_t)active->id,
        (uint32_t)candidate->id,
        package.manifest.image_version,
        options,
        UPDATE_INSTALL_FAULT_METADATA_WRITING
    );
    if (status != UPDATE_INSTALL_OK) {
        return status;
    }

    metadata_status = boot_metadata_recover_from_flash(&restricted, &writing, NULL);
    if (metadata_status != BOOT_METADATA_OK ||
        writing.state != BOOT_METADATA_STATE_WRITING ||
        writing.active_slot != (uint32_t)active->id ||
        writing.candidate_slot != (uint32_t)candidate->id) {
        return UPDATE_INSTALL_ERR_METADATA;
    }

    status = erase_candidate_slot(&restricted, candidate, options, result);
    if (status != UPDATE_INSTALL_OK) {
        return status;
    }

    status = program_candidate_slot(&restricted, candidate, &package, options, result);
    if (status != UPDATE_INSTALL_OK) {
        return status;
    }

    status = hash_installed_payload(&restricted, candidate, &package, options);
    if (status != UPDATE_INSTALL_OK) {
        return status;
    }

    status = verify_installed_package(
        &restricted,
        candidate,
        &package,
        public_key,
        options,
        result
    );
    if (status != UPDATE_INSTALL_OK) {
        return status;
    }

    return commit_metadata_state(
        &restricted,
        &writing,
        BOOT_METADATA_STATE_CANDIDATE_READY,
        (uint32_t)active->id,
        (uint32_t)candidate->id,
        package.manifest.image_version,
        options,
        UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY
    );
}

const char *update_install_status_text(update_install_status_t status)
{
    switch (status) {
    case UPDATE_INSTALL_OK:                       return "OK";
    case UPDATE_INSTALL_ERR_INVALID_ARGUMENT:     return "INVALID ARGUMENT";
    case UPDATE_INSTALL_ERR_PACKAGE:              return "PACKAGE";
    case UPDATE_INSTALL_ERR_METADATA:             return "METADATA";
    case UPDATE_INSTALL_ERR_ACTIVE_STATE:         return "ACTIVE STATE";
    case UPDATE_INSTALL_ERR_SLOT:                 return "SLOT";
    case UPDATE_INSTALL_ERR_ROLLBACK:             return "ROLLBACK";
    case UPDATE_INSTALL_ERR_CAPACITY:             return "CAPACITY";
    case UPDATE_INSTALL_ERR_ERASE:                return "ERASE";
    case UPDATE_INSTALL_ERR_PROGRAM:              return "PROGRAM";
    case UPDATE_INSTALL_ERR_READBACK:             return "READBACK";
    case UPDATE_INSTALL_ERR_HASH:                 return "HASH";
    case UPDATE_INSTALL_ERR_INSTALLED_VERIFY:     return "INSTALLED VERIFY";
    case UPDATE_INSTALL_ERR_INJECTED:             return "INJECTED";
    default:                                      return "UNKNOWN";
    }
}
