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

static update_install_status_t fail_session(
    update_installer_session_t *session,
    update_install_status_t status
)
{
    if ((session != NULL) && (status != UPDATE_INSTALL_OK)) {
        session->state = UPDATE_INSTALL_SESSION_FAILED;
    }

    return status;
}

static uint8_t metadata_matches_start(
    const boot_metadata_record_t *actual,
    const boot_metadata_record_t *expected
)
{
    if ((actual == NULL) || (expected == NULL)) {
        return 0U;
    }

    return ((actual->sequence == expected->sequence) &&
            (actual->state == expected->state) &&
            (actual->active_slot == expected->active_slot) &&
            (actual->candidate_slot == expected->candidate_slot) &&
            (actual->candidate_image_version ==
                expected->candidate_image_version) &&
            (actual->boot_attempt_count == expected->boot_attempt_count) &&
            (actual->confirmation_state == expected->confirmation_state) &&
            (actual->result == expected->result))
        ? 1U
        : 0U;
}

static update_install_status_t candidate_package_fits(
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *candidate,
    const update_package_header_t *header,
    size_t *payload_program_size
)
{
    size_t aligned_package_size = 0U;
    size_t aligned_payload_size = 0U;
    uint32_t slot_program_end = 0U;

    if ((flash == NULL) ||
        (candidate == NULL) ||
        (header == NULL) ||
        (payload_program_size == NULL)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if ((checked_round_up(
            header->package_size,
            (size_t)flash->program_alignment,
            &aligned_package_size
        ) == 0U) ||
        (checked_round_up(
            header->payload_size,
            (size_t)flash->program_alignment,
            &aligned_payload_size
        ) == 0U) ||
        (aligned_package_size > UINT32_MAX) ||
        (checked_u32_add(
            candidate->signed_image_base,
            (uint32_t)aligned_package_size,
            &slot_program_end
        ) == 0U) ||
        (slot_program_end > candidate->slot_end)) {
        return UPDATE_INSTALL_ERR_CAPACITY;
    }

    *payload_program_size = aligned_payload_size;
    return UPDATE_INSTALL_OK;
}

static update_install_status_t program_aligned_block(
    const boot_flash_t *flash,
    uint32_t address,
    const uint8_t *data,
    size_t length,
    const update_install_options_t *options,
    update_install_result_t *result,
    uint32_t *block_index
)
{
    uint32_t detail = 0U;
    update_install_status_t fault_status;

    if ((flash == NULL) ||
        (data == NULL) ||
        (options == NULL) ||
        (block_index == NULL)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    detail = *block_index;
    fault_status =
        maybe_fault(options, UPDATE_INSTALL_FAULT_PROGRAM_BLOCK, detail);
    if (fault_status != UPDATE_INSTALL_OK) {
        return fault_status;
    }

    if (boot_flash_program_aligned(flash, address, data, length) !=
        BOOT_FLASH_OK) {
        return UPDATE_INSTALL_ERR_PROGRAM;
    }

    fault_status = maybe_fault(options, UPDATE_INSTALL_FAULT_READBACK, detail);
    if (fault_status != UPDATE_INSTALL_OK) {
        return fault_status;
    }

    if (boot_flash_read(
            flash,
            address,
            options->readback_buffer,
            length
        ) != BOOT_FLASH_OK) {
        return UPDATE_INSTALL_ERR_READBACK;
    }

    if (memcmp(options->readback_buffer, data, length) != 0) {
        return UPDATE_INSTALL_ERR_READBACK;
    }

    if (*block_index == UINT32_MAX) {
        return UPDATE_INSTALL_ERR_CAPACITY;
    }

    *block_index += 1UL;
    if (result != NULL) {
        result->programmed_block_count += 1UL;
    }

    return UPDATE_INSTALL_OK;
}

static update_install_status_t program_source_range(
    const boot_flash_t *flash,
    uint32_t base_address,
    const uint8_t *source,
    size_t data_size,
    size_t padded_size,
    const update_install_options_t *options,
    update_install_result_t *result,
    uint32_t *block_index
)
{
    size_t offset = 0U;

    if ((flash == NULL) ||
        (source == NULL) ||
        (options == NULL) ||
        (block_index == NULL) ||
        (data_size > padded_size) ||
        (padded_size > UINT32_MAX)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    while (offset < padded_size) {
        size_t chunk = padded_size - offset;
        uint32_t address = 0U;
        update_install_status_t program_status;

        if (chunk > options->program_buffer_size) {
            chunk = options->program_buffer_size;
        }

        memset(options->program_buffer, 0xFF, chunk);
        if (offset < data_size) {
            size_t available = data_size - offset;
            if (available > chunk) {
                available = chunk;
            }
            memcpy(options->program_buffer, &source[offset], available);
        }

        if (checked_u32_add(base_address, (uint32_t)offset, &address) == 0U) {
            return UPDATE_INSTALL_ERR_CAPACITY;
        }

        program_status = program_aligned_block(
            flash,
            address,
            options->program_buffer,
            chunk,
            options,
            result,
            block_index
        );
        if (program_status != UPDATE_INSTALL_OK) {
            return program_status;
        }

        offset += chunk;
    }

    return UPDATE_INSTALL_OK;
}

static update_install_status_t flush_payload_program_buffer(
    update_installer_session_t *session,
    uint8_t final_flush
)
{
    size_t program_length = 0U;
    uint32_t address = 0U;
    update_install_status_t program_status;

    if (session == NULL) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if (session->program_fill == 0U) {
        return UPDATE_INSTALL_OK;
    }

    program_length = session->program_fill;
    if (final_flush != 0U) {
        if (checked_round_up(
                program_length,
                (size_t)session->restricted_flash.program_alignment,
                &program_length
            ) == 0U) {
            return UPDATE_INSTALL_ERR_CAPACITY;
        }
        memset(
            &session->options.program_buffer[session->program_fill],
            0xFF,
            program_length - session->program_fill
        );
    } else if (program_length != session->options.program_buffer_size) {
        return UPDATE_INSTALL_ERR_STATE;
    }

    if ((session->payload_programmed > session->payload_program_size) ||
        (program_length >
        (session->payload_program_size - session->payload_programmed))) {
        return UPDATE_INSTALL_ERR_CAPACITY;
    }

    if (checked_u32_add(
            session->candidate->payload_base,
            (uint32_t)session->payload_programmed,
            &address
        ) == 0U) {
        return UPDATE_INSTALL_ERR_CAPACITY;
    }

    program_status = program_aligned_block(
        &session->restricted_flash,
        address,
        session->options.program_buffer,
        program_length,
        &session->options,
        session->result,
        &session->programmed_block_count
    );
    if (program_status != UPDATE_INSTALL_OK) {
        return program_status;
    }

    session->payload_programmed += program_length;
    session->program_fill = 0U;
    return UPDATE_INSTALL_OK;
}

static update_install_status_t hash_installed_payload(
    const boot_flash_t *flash,
    const boot_slot_descriptor_t *candidate,
    const update_package_header_t *header,
    const update_install_options_t *options
)
{
    crypto_sha512_ctx ctx;
    uint8_t computed[SIGNED_PAYLOAD_HASH_SIZE];
    size_t offset = 0U;

    update_install_status_t fault =
        maybe_fault(options, UPDATE_INSTALL_FAULT_HASH_BEGIN, header->manifest.image_version);
    if (fault != UPDATE_INSTALL_OK) {
        return fault;
    }

    crypto_sha512_init(&ctx);

    while (offset < header->payload_size) {
        size_t chunk = header->payload_size - offset;
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
        maybe_fault(
            options,
            UPDATE_INSTALL_FAULT_HASH_COMPLETE,
            header->manifest.image_version
        );
    if (fault != UPDATE_INSTALL_OK) {
        crypto_wipe(computed, sizeof(computed));
        return fault;
    }

    if (crypto_verify64(computed, header->manifest.payload_sha512) != 0) {
        crypto_wipe(computed, sizeof(computed));
        return UPDATE_INSTALL_ERR_HASH;
    }

    crypto_wipe(computed, sizeof(computed));
    return UPDATE_INSTALL_OK;
}

static uint8_t manifest_matches_header(
    const signed_manifest_t *installed,
    const update_package_header_t *header
)
{
    const signed_manifest_t *expected = NULL;

    if ((installed == NULL) || (header == NULL)) {
        return 0U;
    }

    expected = &header->manifest;
    if ((installed->magic != expected->magic) ||
        (installed->header_version != expected->header_version) ||
        (installed->image_version != expected->image_version) ||
        (installed->vector_address != expected->vector_address) ||
        (installed->image_size != expected->image_size) ||
        (installed->flags != expected->flags) ||
        (installed->reserved0 != expected->reserved0) ||
        (installed->reserved1 != expected->reserved1) ||
        (crypto_verify64(
            installed->payload_sha512,
            expected->payload_sha512
        ) != 0)) {
        return 0U;
    }

    return 1U;
}

static uint8_t installed_header_matches_expected(
    const boot_slot_descriptor_t *candidate,
    const update_package_header_t *header
)
{
    const uint8_t *installed_manifest_bytes = NULL;
    const uint8_t *installed_signature_bytes = NULL;
    const uint8_t *installed_padding = NULL;
    size_t padding_size = 0U;

    if ((candidate == NULL) || (header == NULL)) {
        return 0U;
    }

    installed_manifest_bytes =
        (const uint8_t *)(uintptr_t)candidate->manifest_address;
    installed_signature_bytes =
        (const uint8_t *)(uintptr_t)candidate->signature_address;
    installed_padding =
        (const uint8_t *)(uintptr_t)(
            candidate->signature_address + SIGNED_SIGNATURE_SIZE
        );
    padding_size =
        (size_t)SIGNED_IMAGE_HEADER_SIZE -
        ((size_t)SIGNED_MANIFEST_SIZE + (size_t)SIGNED_SIGNATURE_SIZE);

    if (memcmp(
            installed_manifest_bytes,
            header->manifest_bytes,
            (size_t)SIGNED_MANIFEST_SIZE
        ) != 0) {
        return 0U;
    }

    if (memcmp(
            installed_signature_bytes,
            header->signature_bytes,
            (size_t)SIGNED_SIGNATURE_SIZE
        ) != 0) {
        return 0U;
    }

    for (size_t i = 0U; i < padding_size; ++i) {
        if (installed_padding[i] != 0xFFU) {
            return 0U;
        }
    }

    return 1U;
}

static update_install_status_t verify_installed_package(
    const boot_slot_descriptor_t *candidate,
    const update_package_header_t *header,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
)
{
    const uint8_t *installed_manifest_bytes = NULL;
    const uint8_t *installed_signature_bytes = NULL;
    const uint8_t *installed_payload = NULL;
    signed_manifest_t installed_manifest;
    verify_status_t verify_status = VERIFY_BAD_PAYLOAD_RANGE;

    if ((candidate == NULL) || (header == NULL) || (public_key == NULL)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    update_install_status_t fault_status =
        maybe_fault(
            options,
            UPDATE_INSTALL_FAULT_VERIFY_INSTALLED,
            header->manifest.image_version
        );
    if (fault_status != UPDATE_INSTALL_OK) {
        return fault_status;
    }

    installed_manifest_bytes =
        (const uint8_t *)(uintptr_t)candidate->manifest_address;
    installed_signature_bytes =
        (const uint8_t *)(uintptr_t)candidate->signature_address;
    installed_payload = (const uint8_t *)(uintptr_t)candidate->payload_base;

    verify_status = signed_image_verify_update_slot_buffer(
        installed_manifest_bytes,
        installed_signature_bytes,
        installed_payload,
        (size_t)candidate->maximum_payload_size,
        public_key,
        candidate
    );
    if (verify_status == VERIFY_OK) {
        verify_status = signed_image_decode_manifest(
            installed_manifest_bytes,
            &installed_manifest
        );
        if ((verify_status == VERIFY_OK) &&
            (manifest_matches_header(&installed_manifest, header) == 0U)) {
            verify_status = VERIFY_BAD_SIGNATURE;
        }
        if ((verify_status == VERIFY_OK) &&
            (installed_header_matches_expected(candidate, header) == 0U)) {
            verify_status = VERIFY_BAD_SIGNATURE;
        }
    }

    if (result != NULL) {
        result->installed_verify_status = verify_status;
    }

    return (verify_status == VERIFY_OK)
        ? UPDATE_INSTALL_OK
        : UPDATE_INSTALL_ERR_INSTALLED_VERIFY;
}

static update_install_status_t verify_streaming_payload_hash(
    update_installer_session_t *session
)
{
    uint8_t computed[SIGNED_PAYLOAD_HASH_SIZE];

    if (session == NULL) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    crypto_sha512_final(&session->payload_hash_ctx, computed);
    session->payload_hash_finalized = 1U;

    if (crypto_verify64(computed, session->header.manifest.payload_sha512) !=
        0) {
        crypto_wipe(computed, sizeof(computed));
        return UPDATE_INSTALL_ERR_HASH;
    }

    crypto_wipe(computed, sizeof(computed));
    return UPDATE_INSTALL_OK;
}

update_install_status_t update_installer_session_init(
    update_installer_session_t *session,
    const boot_flash_t *flash,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *options,
    update_install_result_t *result
)
{
    boot_metadata_record_t current;
    boot_metadata_recovery_t recovery;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    update_install_status_t install_status;
    boot_metadata_status_t metadata_status;

    if (session == NULL) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    memset(session, 0, sizeof(*session));
    session->state = UPDATE_INSTALL_SESSION_EMPTY;

    if ((flash == NULL) ||
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

    metadata_status =
        boot_metadata_recover_from_flash(flash, &current, &recovery);
    if (metadata_status != BOOT_METADATA_OK) {
        return fail_session(session, UPDATE_INSTALL_ERR_METADATA);
    }

    install_status = select_inactive_slot(&current, &active, &candidate);
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    install_status = init_restricted_flash(
        flash,
        candidate,
        &session->restricted_flash,
        session->write_regions
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    session->flash = flash;
    session->public_key = public_key;
    session->options = *options;
    session->result = result;
    session->metadata_before = current;
    session->metadata_recovery = recovery;
    session->active = active;
    session->candidate = candidate;
    session->state = UPDATE_INSTALL_SESSION_INITIALIZED;

    if (result != NULL) {
        result->active_slot = (uint32_t)active->id;
        result->candidate_slot = (uint32_t)candidate->id;
        result->metadata_recovery = recovery;
    }

    return UPDATE_INSTALL_OK;
}

update_install_status_t update_installer_begin(
    update_installer_session_t *session,
    const uint8_t *header_bytes,
    size_t header_size
)
{
    boot_metadata_record_t recovered_metadata;
    verify_status_t verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    update_package_status_t package_status;
    update_install_status_t install_status;
    boot_metadata_status_t metadata_status;

    if ((session == NULL) || (header_bytes == NULL)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if (session->state != UPDATE_INSTALL_SESSION_INITIALIZED) {
        return UPDATE_INSTALL_ERR_STATE;
    }

    metadata_status = boot_metadata_recover_from_flash(
        &session->restricted_flash,
        &recovered_metadata,
        NULL
    );
    if (metadata_status != BOOT_METADATA_OK) {
        return fail_session(session, UPDATE_INSTALL_ERR_METADATA);
    }

    if (metadata_matches_start(
            &recovered_metadata,
            &session->metadata_before
        ) == 0U) {
        return fail_session(session, UPDATE_INSTALL_ERR_ACTIVE_STATE);
    }

    package_status = update_package_verify_header_for_slot(
        header_bytes,
        header_size,
        session->public_key,
        session->candidate,
        &session->header,
        &verify_status
    );
    if (session->result != NULL) {
        session->result->package_verify_status = verify_status;
    }
    if (package_status != UPDATE_PACKAGE_OK) {
        return fail_session(session, UPDATE_INSTALL_ERR_PACKAGE);
    }

    if (session->header.manifest.image_version <=
        session->metadata_before.candidate_image_version) {
        return fail_session(session, UPDATE_INSTALL_ERR_ROLLBACK);
    }

    install_status = candidate_package_fits(
        &session->restricted_flash,
        session->candidate,
        &session->header,
        &session->payload_program_size
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    if (session->result != NULL) {
        session->result->image_version = session->header.manifest.image_version;
    }

    install_status = commit_metadata_state(
        &session->restricted_flash,
        &session->metadata_before,
        BOOT_METADATA_STATE_WRITING,
        (uint32_t)session->active->id,
        (uint32_t)session->candidate->id,
        session->header.manifest.image_version,
        &session->options,
        UPDATE_INSTALL_FAULT_METADATA_WRITING
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }
    session->metadata_writing_committed = 1U;

    metadata_status = boot_metadata_recover_from_flash(
        &session->restricted_flash,
        &session->writing_metadata,
        NULL
    );
    if ((metadata_status != BOOT_METADATA_OK) ||
        (session->writing_metadata.state != BOOT_METADATA_STATE_WRITING) ||
        (session->writing_metadata.active_slot !=
            (uint32_t)session->active->id) ||
        (session->writing_metadata.candidate_slot !=
            (uint32_t)session->candidate->id)) {
        return fail_session(session, UPDATE_INSTALL_ERR_METADATA);
    }

    install_status = erase_candidate_slot(
        &session->restricted_flash,
        session->candidate,
        &session->options,
        session->result
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }
    session->candidate_erased = 1U;

    install_status = program_source_range(
        &session->restricted_flash,
        session->candidate->signed_image_base,
        header_bytes,
        (size_t)SIGNED_IMAGE_HEADER_SIZE,
        (size_t)SIGNED_IMAGE_HEADER_SIZE,
        &session->options,
        session->result,
        &session->programmed_block_count
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    crypto_sha512_init(&session->payload_hash_ctx);
    session->state = UPDATE_INSTALL_SESSION_WRITING;
    return UPDATE_INSTALL_OK;
}

update_install_status_t update_installer_write(
    update_installer_session_t *session,
    size_t payload_offset,
    const uint8_t *data,
    size_t length
)
{
    const uint8_t *input = data;
    size_t remaining = length;

    if (session == NULL) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if (session->state == UPDATE_INSTALL_SESSION_PAYLOAD_COMPLETE) {
        return fail_session(session, UPDATE_INSTALL_ERR_SEQUENCE);
    }

    if (session->state != UPDATE_INSTALL_SESSION_WRITING) {
        return UPDATE_INSTALL_ERR_STATE;
    }

    if ((data == NULL) || (length == 0U)) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if (payload_offset != session->payload_received) {
        return fail_session(session, UPDATE_INSTALL_ERR_SEQUENCE);
    }

    if (length > (session->header.payload_size - session->payload_received)) {
        return fail_session(session, UPDATE_INSTALL_ERR_SEQUENCE);
    }

    while (remaining > 0U) {
        size_t buffer_space =
            session->options.program_buffer_size - session->program_fill;
        size_t consumed = remaining;
        update_install_status_t install_status;

        if (consumed > buffer_space) {
            consumed = buffer_space;
        }

        memcpy(
            &session->options.program_buffer[session->program_fill],
            input,
            consumed
        );
        crypto_sha512_update(&session->payload_hash_ctx, input, consumed);

        session->program_fill += consumed;
        session->payload_received += consumed;
        input = &input[consumed];
        remaining -= consumed;

        if (session->program_fill == session->options.program_buffer_size) {
            install_status = flush_payload_program_buffer(session, 0U);
            if (install_status != UPDATE_INSTALL_OK) {
                return fail_session(session, install_status);
            }
        }
    }

    if (session->payload_received == session->header.payload_size) {
        session->state = UPDATE_INSTALL_SESSION_PAYLOAD_COMPLETE;
    }

    return UPDATE_INSTALL_OK;
}

update_install_status_t update_installer_finish(update_installer_session_t *session)
{
    update_install_status_t install_status;

    if (session == NULL) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if (session->state == UPDATE_INSTALL_SESSION_WRITING) {
        return fail_session(session, UPDATE_INSTALL_ERR_SEQUENCE);
    }

    if (session->state != UPDATE_INSTALL_SESSION_PAYLOAD_COMPLETE) {
        return UPDATE_INSTALL_ERR_STATE;
    }

    install_status = flush_payload_program_buffer(session, 1U);
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    if (session->payload_programmed != session->payload_program_size) {
        return fail_session(session, UPDATE_INSTALL_ERR_CAPACITY);
    }

    install_status = verify_streaming_payload_hash(session);
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    install_status = hash_installed_payload(
        &session->restricted_flash,
        session->candidate,
        &session->header,
        &session->options
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    install_status = verify_installed_package(
        session->candidate,
        &session->header,
        session->public_key,
        &session->options,
        session->result
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    install_status = commit_metadata_state(
        &session->restricted_flash,
        &session->writing_metadata,
        BOOT_METADATA_STATE_CANDIDATE_READY,
        (uint32_t)session->active->id,
        (uint32_t)session->candidate->id,
        session->header.manifest.image_version,
        &session->options,
        UPDATE_INSTALL_FAULT_METADATA_CANDIDATE_READY
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return fail_session(session, install_status);
    }

    session->state = UPDATE_INSTALL_SESSION_FINISHED;
    return UPDATE_INSTALL_OK;
}

update_install_status_t update_installer_abort(update_installer_session_t *session)
{
    boot_metadata_record_t current;
    boot_metadata_record_t rejected;
    boot_metadata_status_t metadata_status;

    if (session == NULL) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    if (session->state == UPDATE_INSTALL_SESSION_FINISHED) {
        return UPDATE_INSTALL_ERR_STATE;
    }

    if (session->state == UPDATE_INSTALL_SESSION_ABORTED) {
        return UPDATE_INSTALL_OK;
    }

    if (session->metadata_writing_committed != 0U) {
        metadata_status = boot_metadata_recover_from_flash(
            &session->restricted_flash,
            &current,
            NULL
        );
        if (metadata_status != BOOT_METADATA_OK) {
            session->state = UPDATE_INSTALL_SESSION_FAILED;
            return UPDATE_INSTALL_ERR_METADATA;
        }

        if ((current.state == BOOT_METADATA_STATE_WRITING) &&
            (current.active_slot == (uint32_t)session->active->id) &&
            (current.candidate_slot == (uint32_t)session->candidate->id) &&
            (current.candidate_image_version ==
                session->header.manifest.image_version)) {
            metadata_status = boot_metadata_prepare_next(
                &current,
                BOOT_METADATA_STATE_REJECTED_INVALID,
                (uint32_t)session->active->id,
                (uint32_t)session->candidate->id,
                session->header.manifest.image_version,
                0U,
                0U,
                0U,
                &rejected
            );
            if (metadata_status != BOOT_METADATA_OK) {
                session->state = UPDATE_INSTALL_SESSION_FAILED;
                return UPDATE_INSTALL_ERR_METADATA;
            }

            metadata_status = boot_metadata_commit(
                &session->restricted_flash,
                &rejected
            );
            if (metadata_status != BOOT_METADATA_OK) {
                session->state = UPDATE_INSTALL_SESSION_FAILED;
                return UPDATE_INSTALL_ERR_METADATA;
            }
        } else if (current.state == BOOT_METADATA_STATE_CANDIDATE_READY) {
            session->state = UPDATE_INSTALL_SESSION_FAILED;
            return UPDATE_INSTALL_ERR_STATE;
        }
    }

    session->state = UPDATE_INSTALL_SESSION_ABORTED;
    return UPDATE_INSTALL_OK;
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
    update_installer_session_t session;
    update_package_t package;
    verify_status_t package_verify_status = VERIFY_BAD_PAYLOAD_RANGE;
    update_package_status_t package_status;
    update_install_status_t install_status;
    size_t payload_offset = 0U;

    if (package_bytes == NULL) {
        return UPDATE_INSTALL_ERR_INVALID_ARGUMENT;
    }

    install_status = update_installer_session_init(
        &session,
        flash,
        public_key,
        options,
        result
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return install_status;
    }

    package_status = update_package_verify_for_slot(
        package_bytes,
        package_size,
        public_key,
        session.candidate,
        &package,
        &package_verify_status
    );
    if (result != NULL) {
        result->package_verify_status = package_verify_status;
    }
    if (package_status != UPDATE_PACKAGE_OK) {
        return UPDATE_INSTALL_ERR_PACKAGE;
    }

    install_status = update_installer_begin(
        &session,
        package_bytes,
        (size_t)SIGNED_IMAGE_HEADER_SIZE
    );
    if (install_status != UPDATE_INSTALL_OK) {
        return install_status;
    }

    while (payload_offset < package.payload_size) {
        size_t chunk = package.payload_size - payload_offset;

        if (chunk > options->program_buffer_size) {
            chunk = options->program_buffer_size;
        }

        install_status = update_installer_write(
            &session,
            payload_offset,
            &package.payload[payload_offset],
            chunk
        );
        if (install_status != UPDATE_INSTALL_OK) {
            (void)update_installer_abort(&session);
            return install_status;
        }

        payload_offset += chunk;
    }

    install_status = update_installer_finish(&session);
    if (install_status != UPDATE_INSTALL_OK) {
        (void)update_installer_abort(&session);
        return install_status;
    }

    return UPDATE_INSTALL_OK;
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
    case UPDATE_INSTALL_ERR_STATE:                return "STATE";
    case UPDATE_INSTALL_ERR_SEQUENCE:             return "SEQUENCE";
    default:                                      return "UNKNOWN";
    }
}
