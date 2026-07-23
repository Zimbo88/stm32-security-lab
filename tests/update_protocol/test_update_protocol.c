#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_flash.h"
#include "boot_metadata.h"
#include "boot_slot.h"
#include "monocypher-ed25519.h"
#include "simulated_flash.h"
#include "stm32f429_memory_layout.h"
#include "update_installer.h"
#include "update_protocol.h"
#include "update_service.h"

#define TEST_UPDATE_PAYLOAD_SIZE 1536U
#define TEST_ACTIVE_PAYLOAD_SIZE 256U
#define TEST_PACKAGE_CAPACITY \
    (SIGNED_IMAGE_HEADER_SIZE + TEST_UPDATE_PAYLOAD_SIZE)
#define TEST_ACTIVE_PACKAGE_CAPACITY \
    (SIGNED_IMAGE_HEADER_SIZE + TEST_ACTIVE_PAYLOAD_SIZE)
#define TEST_PROGRAM_BUFFER_SIZE 128U
#define TEST_CAPTURE_CAPACITY 8192U

static int failures;
static uint8_t update_secret_key[64];
static uint8_t update_public_key[FIRMWARE_PUBLIC_KEY_SIZE];

static const boot_flash_region_t installer_write_regions[] = {
    {STM32F429_BOOT_METADATA_A_BASE, STM32F429_BOOT_METADATA_A_END},
    {STM32F429_BOOT_METADATA_B_BASE, STM32F429_BOOT_METADATA_B_END},
    {STM32F429_SLOT_A_SIGNED_IMAGE_BASE, STM32F429_SLOT_A_END},
    {STM32F429_SLOT_B_SIGNED_IMAGE_BASE, STM32F429_SLOT_B_END},
};

typedef struct {
    uint8_t data[TEST_CAPTURE_CAPACITY];
    size_t length;
    uint32_t write_call_count;
    uint32_t fail_on_write_call;
} capture_writer_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} array_reader_t;

typedef struct {
    uint32_t count;
} reset_capture_t;

typedef struct {
    update_install_fault_point_t point;
    update_install_status_t status;
    uint8_t fired;
} protocol_fault_config_t;

static void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
    if (expected != actual) {
        printf(
            "%s: expected 0x%08X, got 0x%08X\n",
            name,
            (unsigned int)expected,
            (unsigned int)actual
        );
        ++failures;
    }
}

static void expect_size(const char *name, size_t expected, size_t actual)
{
    if (expected != actual) {
        printf(
            "%s: expected %lu, got %lu\n",
            name,
            (unsigned long)expected,
            (unsigned long)actual
        );
        ++failures;
    }
}

static void expect_protocol_status(
    const char *name,
    update_protocol_status_t expected,
    update_protocol_status_t actual
)
{
    if (expected != actual) {
        printf(
            "%s: expected %s (%d), got %s (%d)\n",
            name,
            update_protocol_status_text(expected),
            (int)expected,
            update_protocol_status_text(actual),
            (int)actual
        );
        ++failures;
    }
}

static void expect_parse_status(
    const char *name,
    update_protocol_parse_status_t expected,
    update_protocol_parse_status_t actual
)
{
    if (expected != actual) {
        printf(
            "%s: expected parse %d, got %d\n",
            name,
            (int)expected,
            (int)actual
        );
        ++failures;
    }
}

static void expect_metadata_status(
    const char *name,
    boot_metadata_status_t expected,
    boot_metadata_status_t actual
)
{
    if (expected != actual) {
        printf(
            "%s: expected metadata %s, got %s\n",
            name,
            boot_metadata_status_text(expected),
            boot_metadata_status_text(actual)
        );
        ++failures;
    }
}

static void expect_flash_status(
    const char *name,
    boot_flash_status_t expected,
    boot_flash_status_t actual
)
{
    if (expected != actual) {
        printf(
            "%s: expected flash %s, got %s\n",
            name,
            boot_flash_status_text(expected),
            boot_flash_status_text(actual)
        );
        ++failures;
    }
}

static uint16_t load_le16(const uint8_t bytes[2])
{
    return (uint16_t)(((uint16_t)bytes[0]) |
        ((uint16_t)bytes[1] << 8));
}

static void store_le16(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void store_le32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFU);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFU);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static update_protocol_status_t capture_write(
    void *context,
    const uint8_t *data,
    size_t length
)
{
    capture_writer_t *capture = (capture_writer_t *)context;

    if ((capture == NULL) ||
        ((length != 0U) && (data == NULL)) ||
        (length > (sizeof(capture->data) - capture->length))) {
        return UPDATE_PROTOCOL_STATUS_IO_ERROR;
    }

    capture->write_call_count += 1UL;
    if ((capture->fail_on_write_call != 0U) &&
        (capture->write_call_count == capture->fail_on_write_call)) {
        return UPDATE_PROTOCOL_STATUS_IO_ERROR;
    }

    if (length != 0U) {
        memcpy(&capture->data[capture->length], data, length);
        capture->length += length;
    }
    return UPDATE_PROTOCOL_STATUS_OK;
}

static void capture_reset(void *context)
{
    reset_capture_t *reset = (reset_capture_t *)context;

    if (reset != NULL) {
        reset->count += 1UL;
    }
}

static byte_reader_status_t array_getc(void *context, uint8_t *byte)
{
    array_reader_t *reader = (array_reader_t *)context;

    if ((reader == NULL) || (byte == NULL)) {
        return BYTE_READER_ERR_INVALID_ARGUMENT;
    }
    if (reader->offset >= reader->length) {
        return BYTE_READER_NO_DATA;
    }

    *byte = reader->data[reader->offset++];
    return BYTE_READER_OK;
}

static update_install_status_t protocol_fault_hook(
    void *context,
    update_install_fault_point_t point,
    uint32_t detail
)
{
    protocol_fault_config_t *config = (protocol_fault_config_t *)context;

    (void)detail;
    if ((config != NULL) && (config->fired == 0U) &&
        (config->point == point)) {
        config->fired = 1U;
        return config->status;
    }

    return UPDATE_INSTALL_OK;
}

static void init_update_keys(void)
{
    uint8_t seed[32];

    for (uint32_t i = 0U; i < sizeof(seed); ++i) {
        seed[i] = (uint8_t)(0xA0U + i);
    }

    crypto_ed25519_key_pair(update_secret_key, update_public_key, seed);
}

static void sign_package_manifest(uint8_t *package)
{
    crypto_ed25519_sign(
        &package[SIGNED_MANIFEST_SIZE],
        update_secret_key,
        package,
        SIGNED_MANIFEST_SIZE
    );
}

static size_t flash_offset(uint32_t address)
{
    return (size_t)(address - STM32F429_FLASH_BASE);
}

static void install_package_bytes_direct(
    simulated_flash_t *sim,
    const boot_slot_descriptor_t *slot,
    const uint8_t *package,
    size_t package_size
)
{
    memcpy(
        &sim->storage[flash_offset(slot->signed_image_base)],
        package,
        package_size
    );
    simulated_flash_sync_mapped(sim);
}

static void make_installer_flash(simulated_flash_t *sim, boot_flash_t *flash)
{
    simulated_flash_init(sim);
    expect_flash_status(
        "protocol installer flash init",
        BOOT_FLASH_OK,
        boot_flash_init(
            flash,
            sim,
            simulated_flash_ops(),
            installer_write_regions,
            sizeof(installer_write_regions) / sizeof(installer_write_regions[0]),
            8U
        )
    );
}

static boot_metadata_record_t metadata_empty_record(void)
{
    boot_metadata_record_t record;

    expect_metadata_status(
        "protocol empty metadata",
        BOOT_METADATA_OK,
        boot_metadata_empty(&record)
    );
    return record;
}

static boot_metadata_record_t metadata_confirmed(
    const boot_metadata_record_t *current,
    uint32_t slot,
    uint32_t version
)
{
    boot_metadata_record_t next;

    expect_metadata_status(
        "protocol confirmed metadata",
        BOOT_METADATA_OK,
        boot_metadata_prepare_next(
            current,
            BOOT_METADATA_STATE_CONFIRMED,
            slot,
            BOOT_SLOT_NONE,
            version,
            0U,
            1U,
            0U,
            &next
        )
    );
    return next;
}

static void commit_confirmed_metadata(
    boot_flash_t *flash,
    uint32_t active_slot,
    uint32_t version
)
{
    boot_metadata_record_t empty = metadata_empty_record();
    boot_metadata_record_t confirmed =
        metadata_confirmed(&empty, active_slot, version);

    expect_metadata_status(
        "protocol commit confirmed",
        BOOT_METADATA_OK,
        boot_metadata_commit(flash, &confirmed)
    );
}

static size_t build_update_package_for_slot(
    uint32_t slot_id,
    uint32_t image_version,
    size_t payload_size,
    uint8_t *package,
    size_t package_capacity
);

static void prepare_confirmed_active_image(
    simulated_flash_t *sim,
    boot_flash_t *flash,
    uint32_t active_slot_id,
    uint32_t version,
    uint8_t *package,
    size_t package_capacity
)
{
    const boot_slot_descriptor_t *active = NULL;
    const size_t package_size = build_update_package_for_slot(
        active_slot_id,
        version,
        TEST_ACTIVE_PAYLOAD_SIZE,
        package,
        package_capacity
    );

    expect_u32(
        "protocol confirmed active lookup",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(active_slot_id, &active)
    );
    if (active != NULL) {
        install_package_bytes_direct(sim, active, package, package_size);
    }
    commit_confirmed_metadata(flash, active_slot_id, version);
}

static size_t build_update_package_for_slot(
    uint32_t slot_id,
    uint32_t image_version,
    size_t payload_size,
    uint8_t *package,
    size_t package_capacity
)
{
    const boot_slot_descriptor_t *slot = NULL;
    uint8_t *payload = NULL;
    uint8_t payload_hash[SIGNED_PAYLOAD_HASH_SIZE];
    const size_t package_size =
        (size_t)SIGNED_IMAGE_HEADER_SIZE + payload_size;

    expect_u32(
        "protocol package capacity",
        1U,
        (package_capacity >= package_size) ? 1U : 0U
    );
    expect_u32(
        "protocol slot lookup",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(slot_id, &slot)
    );

    memset(package, 0xFF, package_size);
    payload = &package[SIGNED_IMAGE_HEADER_SIZE];
    memset(payload, 0xA5, payload_size);
    store_le32(&payload[0], APPLICATION_MSP_END);
    store_le32(&payload[4], slot->payload_base | 1UL);
    crypto_sha512(payload_hash, payload, payload_size);

    store_le32(&package[offsetof(signed_manifest_t, magic)], SIGNED_IMAGE_MAGIC);
    store_le32(
        &package[offsetof(signed_manifest_t, header_version)],
        UPDATE_PACKAGE_FORMAT_VERSION
    );
    store_le32(
        &package[offsetof(signed_manifest_t, image_version)],
        image_version
    );
    store_le32(
        &package[offsetof(signed_manifest_t, vector_address)],
        slot->payload_base
    );
    store_le32(
        &package[offsetof(signed_manifest_t, image_size)],
        (uint32_t)payload_size
    );
    store_le32(&package[offsetof(signed_manifest_t, flags)], 0U);
    store_le32(
        &package[offsetof(signed_manifest_t, reserved0)],
        UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1
    );
    store_le32(
        &package[offsetof(signed_manifest_t, reserved1)],
        UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION
    );
    memcpy(
        &package[offsetof(signed_manifest_t, payload_sha512)],
        payload_hash,
        sizeof(payload_hash)
    );
    sign_package_manifest(package);
    return package_size;
}

static update_install_options_t make_install_options(
    uint8_t *program_buffer,
    uint8_t *readback_buffer
)
{
    update_install_options_t options;

    options.program_buffer = program_buffer;
    options.program_buffer_size = TEST_PROGRAM_BUFFER_SIZE;
    options.readback_buffer = readback_buffer;
    options.readback_buffer_size = TEST_PROGRAM_BUFFER_SIZE;
    options.fault_hook = NULL;
    options.fault_context = NULL;
    return options;
}

static void make_session(
    update_protocol_session_t *session,
    boot_flash_t *flash,
    update_install_options_t *install_options,
    capture_writer_t *capture
)
{
    memset(capture, 0, sizeof(*capture));
    update_protocol_session_init(
        session,
        flash,
        update_public_key,
        install_options,
        capture_write,
        capture
    );
}

static void make_service_config(
    update_service_config_t *config,
    boot_flash_t *flash,
    update_install_options_t *install_options,
    byte_reader_t *reader,
    capture_writer_t *capture,
    reset_capture_t *reset
)
{
    memset(config, 0, sizeof(*config));
    config->flash = flash;
    config->public_key = update_public_key;
    config->install_options = *install_options;
    config->reader = reader;
    config->write = capture_write;
    config->write_context = capture;
    config->reset = capture_reset;
    config->reset_context = reset;
    config->entry_idle_polls = 4U;
    config->entry_max_bytes = UPDATE_PROTOCOL_MAX_FRAME_SIZE * 2U;
    config->frame_byte_timeout_polls = 2U;
    config->max_frame_timeouts = 2U;
}

static void expect_single_response(
    capture_writer_t *capture,
    uint8_t expected_response,
    uint8_t expected_request,
    update_protocol_status_t expected_status
);

static size_t make_frame(
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t output[UPDATE_PROTOCOL_MAX_FRAME_SIZE]
)
{
    size_t frame_size = 0U;

    expect_protocol_status(
        "encode test frame",
        UPDATE_PROTOCOL_STATUS_OK,
        update_protocol_encode_frame(
            command,
            sequence,
            payload,
            payload_length,
            output,
            UPDATE_PROTOCOL_MAX_FRAME_SIZE,
            &frame_size
        )
    );
    return frame_size;
}

static update_protocol_status_t feed_bytes(
    update_protocol_session_t *session,
    const uint8_t *data,
    size_t length
)
{
    update_protocol_status_t status = UPDATE_PROTOCOL_STATUS_OK;

    for (size_t i = 0U; i < length; ++i) {
        status = update_protocol_session_process_byte(session, data[i]);
    }
    return status;
}

static update_protocol_status_t send_command(
    update_protocol_session_t *session,
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length
)
{
    uint8_t frame[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    const size_t frame_size =
        make_frame(command, sequence, payload, payload_length, frame);

    return feed_bytes(session, frame, frame_size);
}

static update_protocol_status_t send_command_via_reader(
    update_protocol_session_t *session,
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length
)
{
    uint8_t frame[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    array_reader_t array_reader;
    byte_reader_t reader;
    const size_t frame_size =
        make_frame(command, sequence, payload, payload_length, frame);

    array_reader.data = frame;
    array_reader.length = frame_size;
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    return update_protocol_session_process_reader(session, &reader, 1U);
}

static update_protocol_status_t send_write_block(
    update_protocol_session_t *session,
    uint16_t sequence,
    size_t payload_offset,
    const uint8_t *data,
    size_t length,
    uint8_t via_reader
)
{
    uint8_t write_payload[UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE];

    if ((data == NULL) ||
        (length > UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE) ||
        (payload_offset > UINT32_MAX)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    store_le32(write_payload, (uint32_t)payload_offset);
    if (length != 0U) {
        memcpy(
            &write_payload[UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE],
            data,
            length
        );
    }

    if (via_reader != 0U) {
        return send_command_via_reader(
            session,
            UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
            sequence,
            write_payload,
            (uint16_t)(length + UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE)
        );
    }

    return send_command(
        session,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        sequence,
        write_payload,
        (uint16_t)(length + UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE)
    );
}

static void append_frame_to_stream(
    uint8_t *stream,
    size_t stream_capacity,
    size_t *stream_size,
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length
)
{
    uint8_t frame[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    const size_t frame_size =
        make_frame(command, sequence, payload, payload_length, frame);

    expect_u32(
        "append frame capacity",
        1U,
        ((stream != NULL) &&
         (stream_size != NULL) &&
         (frame_size <= (stream_capacity - *stream_size))) ? 1U : 0U
    );
    if ((stream != NULL) && (stream_size != NULL) &&
        (frame_size <= (stream_capacity - *stream_size))) {
        memcpy(&stream[*stream_size], frame, frame_size);
        *stream_size += frame_size;
    }
}

static void append_write_block_to_stream(
    uint8_t *stream,
    size_t stream_capacity,
    size_t *stream_size,
    uint16_t sequence,
    size_t payload_offset,
    const uint8_t *data,
    size_t length
)
{
    uint8_t write_payload[UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE];

    expect_u32(
        "append write block valid",
        1U,
        ((data != NULL) &&
         (length <= UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE) &&
         (payload_offset <= UINT32_MAX)) ? 1U : 0U
    );
    if ((data == NULL) ||
        (length > UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE) ||
        (payload_offset > UINT32_MAX)) {
        return;
    }

    store_le32(write_payload, (uint32_t)payload_offset);
    memcpy(
        &write_payload[UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE],
        data,
        length
    );
    append_frame_to_stream(
        stream,
        stream_capacity,
        stream_size,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        sequence,
        write_payload,
        (uint16_t)(length + UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE)
    );
}

static void append_payload_chunks_to_stream(
    uint8_t *stream,
    size_t stream_capacity,
    size_t *stream_size,
    uint16_t *sequence,
    const uint8_t *payload,
    size_t payload_size,
    size_t chunk_size
)
{
    size_t payload_offset = 0U;

    while (payload_offset < payload_size) {
        size_t chunk = payload_size - payload_offset;
        if (chunk > chunk_size) {
            chunk = chunk_size;
        }

        append_write_block_to_stream(
            stream,
            stream_capacity,
            stream_size,
            *sequence,
            payload_offset,
            &payload[payload_offset],
            chunk
        );
        *sequence = (uint16_t)(*sequence + 1U);
        payload_offset += chunk;
    }
}

static void send_payload_in_chunks(
    update_protocol_session_t *session,
    capture_writer_t *capture,
    uint16_t *sequence,
    const uint8_t *payload,
    size_t payload_size,
    size_t chunk_size,
    uint8_t via_reader
)
{
    size_t payload_offset = 0U;

    expect_u32(
        "protocol chunk size valid",
        1U,
        ((chunk_size != 0U) &&
         (chunk_size <= UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE)) ? 1U : 0U
    );

    while (payload_offset < payload_size) {
        size_t chunk = payload_size - payload_offset;
        if (chunk > chunk_size) {
            chunk = chunk_size;
        }

        expect_protocol_status(
            "protocol write chunk",
            UPDATE_PROTOCOL_STATUS_OK,
            send_write_block(
                session,
                *sequence,
                payload_offset,
                &payload[payload_offset],
                chunk,
                via_reader
            )
        );
        *sequence = (uint16_t)(*sequence + 1U);
        expect_single_response(
            capture,
            UPDATE_PROTOCOL_CMD_ACK,
            UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
            UPDATE_PROTOCOL_STATUS_OK
        );
        payload_offset += chunk;
    }
}

static void parse_next_response(
    const capture_writer_t *capture,
    size_t *offset,
    update_protocol_frame_t *frame
)
{
    update_protocol_parser_t parser;
    update_protocol_parse_status_t status = UPDATE_PROTOCOL_PARSE_NEED_MORE;

    update_protocol_parser_init(&parser);
    while ((*offset < capture->length) &&
        (status == UPDATE_PROTOCOL_PARSE_NEED_MORE)) {
        status = update_protocol_parser_push(&parser, capture->data[*offset]);
        *offset += 1U;
    }

    expect_parse_status(
        "parse response frame",
        UPDATE_PROTOCOL_PARSE_FRAME_READY,
        status
    );
    if (status == UPDATE_PROTOCOL_PARSE_FRAME_READY) {
        *frame = *update_protocol_parser_frame(&parser);
    } else {
        memset(frame, 0, sizeof(*frame));
    }
}

static void parse_last_response(
    const capture_writer_t *capture,
    update_protocol_frame_t *frame
)
{
    size_t offset = 0U;

    memset(frame, 0, sizeof(*frame));
    while (offset < capture->length) {
        parse_next_response(capture, &offset, frame);
    }
}

static update_protocol_status_t response_status(
    const update_protocol_frame_t *frame,
    uint8_t expected_response,
    uint8_t expected_request
)
{
    expect_u32("response command", expected_response, frame->command);
    expect_u32("response version", UPDATE_PROTOCOL_VERSION, frame->version);
    expect_u32(
        "response common length",
        1U,
        (frame->payload_length >= 3U) ? 1U : 0U
    );
    if (frame->payload_length < 3U) {
        return UPDATE_PROTOCOL_STATUS_PARSER_ERROR;
    }
    expect_u32("response request command", expected_request, frame->payload[0]);
    return (update_protocol_status_t)load_le16(&frame->payload[1]);
}

static void expect_single_response(
    capture_writer_t *capture,
    uint8_t expected_response,
    uint8_t expected_request,
    update_protocol_status_t expected_status
)
{
    update_protocol_frame_t response;
    size_t offset = 0U;
    const update_protocol_status_t status = UPDATE_PROTOCOL_STATUS_OK;

    (void)status;
    parse_next_response(capture, &offset, &response);
    expect_protocol_status(
        "response status",
        expected_status,
        response_status(&response, expected_response, expected_request)
    );
    expect_size("one response consumed", capture->length, offset);
    capture->length = 0U;
}

static void test_parser_byte_by_byte_and_fragments(void)
{
    update_protocol_parser_t parser;
    uint8_t payload[3] = {1U, 2U, 3U};
    uint8_t frame[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = make_frame(
        UPDATE_PROTOCOL_CMD_HELLO,
        7U,
        payload,
        sizeof(payload),
        frame
    );
    update_protocol_parse_status_t status = UPDATE_PROTOCOL_PARSE_NEED_MORE;

    update_protocol_parser_init(&parser);
    for (size_t i = 0U; i < frame_size; ++i) {
        status = update_protocol_parser_push(&parser, frame[i]);
        if (i + 1U < frame_size) {
            expect_parse_status(
                "byte parser needs more",
                UPDATE_PROTOCOL_PARSE_NEED_MORE,
                status
            );
        }
    }

    expect_parse_status(
        "byte parser ready",
        UPDATE_PROTOCOL_PARSE_FRAME_READY,
        status
    );
    const update_protocol_frame_t *parsed =
        update_protocol_parser_frame(&parser);
    expect_u32("byte parser command", UPDATE_PROTOCOL_CMD_HELLO, parsed->command);
    expect_u32("byte parser sequence", 7U, parsed->sequence);
    expect_u32("byte parser payload length", sizeof(payload), parsed->payload_length);
    expect_u32("byte parser payload byte", 2U, parsed->payload[1]);
}

static void test_session_fragmented_frame_input(void)
{
    update_protocol_session_t session;
    capture_writer_t capture;
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    uint8_t frame[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0U;
    boot_flash_t flash;
    simulated_flash_t sim;

    make_installer_flash(&sim, &flash);
    make_session(&session, &flash, &options, &capture);
    frame_size = make_frame(UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U, frame);

    expect_protocol_status(
        "fragment first half",
        UPDATE_PROTOCOL_STATUS_OK,
        feed_bytes(&session, frame, 5U)
    );
    expect_size("fragment no early response", 0U, capture.length);

    expect_protocol_status(
        "fragment second half",
        UPDATE_PROTOCOL_STATUS_OK,
        feed_bytes(&session, &frame[5], frame_size - 5U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );
}

static void test_session_multiple_frames_and_resync(void)
{
    update_protocol_session_t session;
    capture_writer_t capture;
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    uint8_t hello[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t info[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t stream[2U * UPDATE_PROTOCOL_MAX_FRAME_SIZE + 8U];
    size_t stream_size = 0U;
    boot_flash_t flash;
    simulated_flash_t sim;
    size_t offset = 0U;
    update_protocol_frame_t response;

    make_installer_flash(&sim, &flash);
    make_session(&session, &flash, &options, &capture);

    memcpy(stream, "junk", 4U);
    stream_size = 4U;
    stream_size += make_frame(
        UPDATE_PROTOCOL_CMD_HELLO,
        0U,
        NULL,
        0U,
        &hello[0]
    );
    memcpy(&stream[4], hello, stream_size - 4U);
    memcpy(&stream[stream_size], info, make_frame(
        UPDATE_PROTOCOL_CMD_GET_INFO,
        1U,
        NULL,
        0U,
        info
    ));
    stream_size += UPDATE_PROTOCOL_HEADER_SIZE + UPDATE_PROTOCOL_CRC_SIZE;

    expect_protocol_status(
        "multi frame feed",
        UPDATE_PROTOCOL_STATUS_OK,
        feed_bytes(&session, stream, stream_size)
    );

    parse_next_response(&capture, &offset, &response);
    expect_protocol_status(
        "multi hello ack",
        UPDATE_PROTOCOL_STATUS_OK,
        response_status(
            &response,
            UPDATE_PROTOCOL_CMD_ACK,
            UPDATE_PROTOCOL_CMD_HELLO
        )
    );
    parse_next_response(&capture, &offset, &response);
    expect_protocol_status(
        "multi info ack",
        UPDATE_PROTOCOL_STATUS_OK,
        response_status(
            &response,
            UPDATE_PROTOCOL_CMD_ACK,
            UPDATE_PROTOCOL_CMD_GET_INFO
        )
    );
    expect_size("multi responses consumed", capture.length, offset);
}

static void test_invalid_frames_fail_closed(void)
{
    update_protocol_session_t session;
    capture_writer_t capture;
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    uint8_t frame[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_size = 0U;
    boot_flash_t flash;
    simulated_flash_t sim;
    uint8_t one_byte = 0x42U;
    uint32_t crc = 0U;

    make_installer_flash(&sim, &flash);
    make_session(&session, &flash, &options, &capture);

    expect_protocol_status(
        "bad magic feed",
        UPDATE_PROTOCOL_STATUS_OK,
        feed_bytes(&session, (const uint8_t *)"BAD!", 4U)
    );
    expect_size("bad magic no response", 0U, capture.length);

    frame_size = make_frame(UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U, frame);
    frame[4] = 2U;
    crc = update_protocol_crc32(frame, frame_size - UPDATE_PROTOCOL_CRC_SIZE);
    store_le32(&frame[frame_size - UPDATE_PROTOCOL_CRC_SIZE], crc);
    expect_protocol_status(
        "wrong version feed",
        UPDATE_PROTOCOL_STATUS_OK,
        feed_bytes(&session, frame, frame_size)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_VERSION_ERROR
    );
    expect_protocol_status(
        "wrong version does not advance sequence",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );

    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "wrong command length feed",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_GET_INFO,
            0U,
            &one_byte,
            sizeof(one_byte)
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_GET_INFO,
        UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
    );

    make_session(&session, &flash, &options, &capture);
    memcpy(frame, "SUPD", 4U);
    frame[4] = UPDATE_PROTOCOL_VERSION;
    frame[5] = UPDATE_PROTOCOL_CMD_HELLO;
    store_le16(&frame[6], 0U);
    store_le16(&frame[8], UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE + 1U);
    expect_protocol_status(
        "oversize feed",
        UPDATE_PROTOCOL_STATUS_OK,
        feed_bytes(&session, frame, UPDATE_PROTOCOL_HEADER_SIZE)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OVERSIZE_ERROR
    );
    expect_protocol_status(
        "oversize does not advance sequence",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );

    make_session(&session, &flash, &options, &capture);
    frame_size = make_frame(UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U, frame);
    frame[frame_size - 1U] ^= 0x80U;
    expect_protocol_status(
        "bad crc feed",
        UPDATE_PROTOCOL_STATUS_OK,
        feed_bytes(&session, frame, frame_size)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_CRC_ERROR
    );
    expect_protocol_status(
        "crc error does not advance sequence",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );

    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "unknown command feed",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, 0x55U, 0U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        0x55U,
        UPDATE_PROTOCOL_STATUS_UNKNOWN_COMMAND
    );
}

static void test_sequence_and_timeout_errors(void)
{
    update_protocol_session_t session;
    capture_writer_t capture;
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    uint8_t frame[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t partial_header[UPDATE_PROTOCOL_HEADER_SIZE];
    boot_flash_t flash;
    simulated_flash_t sim;
    array_reader_t array_reader;
    byte_reader_t reader;

    make_installer_flash(&sim, &flash);
    make_session(&session, &flash, &options, &capture);

    expect_protocol_status(
        "initial hello",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );

    expect_protocol_status(
        "duplicate sequence",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
    );

    expect_protocol_status(
        "skipped sequence",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, UPDATE_PROTOCOL_CMD_HELLO, 2U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
    );

    make_session(&session, &flash, &options, &capture);
    memcpy(partial_header, "SUPD", 4U);
    partial_header[4] = UPDATE_PROTOCOL_VERSION;
    partial_header[5] = UPDATE_PROTOCOL_CMD_GET_INFO;
    store_le16(&partial_header[6], 0U);
    store_le16(&partial_header[8], 1U);
    array_reader.data = partial_header;
    array_reader.length = sizeof(partial_header);
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    expect_protocol_status(
        "mid-frame timeout",
        UPDATE_PROTOCOL_STATUS_TIMEOUT,
        update_protocol_session_process_reader(&session, &reader, 1U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_GET_INFO,
        UPDATE_PROTOCOL_STATUS_TIMEOUT
    );

    make_session(&session, &flash, &options, &capture);
    uint32_t random = 0x12345678UL;
    for (uint32_t i = 0U; i < 4096U; ++i) {
        random = (random * 1664525UL) + 1013904223UL;
        (void)update_protocol_session_process_byte(
            &session,
            (uint8_t)(random >> 24)
        );
    }
    expect_protocol_status(
        "valid after random input",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(&session, UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );

    (void)make_frame(UPDATE_PROTOCOL_CMD_HELLO, 0U, NULL, 0U, frame);
}

static void test_update_command_state_errors(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t active_package[TEST_ACTIVE_PACKAGE_CAPACITY];
    uint8_t update_package[TEST_PACKAGE_CAPACITY];
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t one_byte = 0x5AU;
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_protocol_session_t session;
    capture_writer_t capture;
    uint16_t sequence = 0U;

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    (void)build_update_package_for_slot(
        BOOT_SLOT_B,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        update_package,
        sizeof(update_package)
    );

    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "finish before begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
            0U,
            NULL,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
        UPDATE_PROTOCOL_STATUS_STATE_ERROR
    );

    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "write before begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_write_block(&session, 0U, 0U, &one_byte, sizeof(one_byte), 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        UPDATE_PROTOCOL_STATUS_STATE_ERROR
    );

    make_session(&session, &flash, &options, &capture);
    sequence = 0U;
    expect_protocol_status(
        "begin for repeated begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_protocol_status(
        "repeated begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_STATE_ERROR
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    make_session(&session, &flash, &options, &capture);
    sequence = 0U;
    expect_protocol_status(
        "begin for premature finish",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_protocol_status(
        "premature finish",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
            sequence++,
            NULL,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
        UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    make_session(&session, &flash, &options, &capture);
    sequence = 0U;
    expect_protocol_status(
        "begin for skipped block",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_protocol_status(
        "skipped block offset",
        UPDATE_PROTOCOL_STATUS_OK,
        send_write_block(
            &session,
            sequence++,
            64U,
            &update_package[SIGNED_IMAGE_HEADER_SIZE + 64U],
            64U,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    make_session(&session, &flash, &options, &capture);
    sequence = 0U;
    expect_protocol_status(
        "begin for duplicate block",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_protocol_status(
        "first block before duplicate",
        UPDATE_PROTOCOL_STATUS_OK,
        send_write_block(
            &session,
            sequence++,
            0U,
            &update_package[SIGNED_IMAGE_HEADER_SIZE],
            64U,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_protocol_status(
        "duplicate block offset",
        UPDATE_PROTOCOL_STATUS_OK,
        send_write_block(
            &session,
            sequence++,
            0U,
            &update_package[SIGNED_IMAGE_HEADER_SIZE],
            64U,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    make_session(&session, &flash, &options, &capture);
    sequence = 0U;
    expect_protocol_status(
        "begin before extra data",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    send_payload_in_chunks(
        &session,
        &capture,
        &sequence,
        &update_package[SIGNED_IMAGE_HEADER_SIZE],
        TEST_UPDATE_PAYLOAD_SIZE,
        128U,
        0U
    );
    expect_protocol_status(
        "extra payload byte",
        UPDATE_PROTOCOL_STATUS_OK,
        send_write_block(
            &session,
            sequence++,
            TEST_UPDATE_PAYLOAD_SIZE,
            &one_byte,
            sizeof(one_byte),
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    make_session(&session, &flash, &options, &capture);
    sequence = 0U;
    expect_protocol_status(
        "begin before abort",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_protocol_status(
        "abort update",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_ABORT_UPDATE,
            sequence++,
            NULL,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_ABORT_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_protocol_status(
        "write after abort",
        UPDATE_PROTOCOL_STATUS_OK,
        send_write_block(&session, sequence++, 0U, &one_byte, sizeof(one_byte), 0U)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        UPDATE_PROTOCOL_STATUS_STATE_ERROR
    );
}

static void test_update_verify_rollback_and_flash_errors(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t active_package[TEST_ACTIVE_PACKAGE_CAPACITY];
    uint8_t update_package[TEST_PACKAGE_CAPACITY];
    uint8_t tampered_package[TEST_PACKAGE_CAPACITY];
    uint8_t rollback_package[TEST_PACKAGE_CAPACITY];
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_protocol_session_t session;
    capture_writer_t capture;
    boot_metadata_record_t metadata;
    protocol_fault_config_t fault;
    uint16_t sequence = 0U;

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        5U,
        active_package,
        sizeof(active_package)
    );
    (void)build_update_package_for_slot(
        BOOT_SLOT_B,
        6U,
        TEST_UPDATE_PAYLOAD_SIZE,
        update_package,
        sizeof(update_package)
    );

    memcpy(tampered_package, update_package, sizeof(tampered_package));
    tampered_package[SIGNED_MANIFEST_SIZE] ^= 0x01U;
    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "bad signature begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            0U,
            tampered_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_VERIFY_ERROR
    );
    expect_metadata_status(
        "bad signature metadata readable",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &metadata, NULL)
    );
    expect_u32(
        "bad signature leaves confirmed metadata",
        BOOT_METADATA_STATE_CONFIRMED,
        metadata.state
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        5U,
        active_package,
        sizeof(active_package)
    );
    memcpy(tampered_package, update_package, sizeof(tampered_package));
    tampered_package[SIGNED_IMAGE_HEADER_SIZE + 17U] ^= 0x80U;
    make_session(&session, &flash, &options, &capture);
    sequence = 0U;
    expect_protocol_status(
        "bad hash begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            tampered_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );
    send_payload_in_chunks(
        &session,
        &capture,
        &sequence,
        &tampered_package[SIGNED_IMAGE_HEADER_SIZE],
        TEST_UPDATE_PAYLOAD_SIZE,
        256U,
        0U
    );
    expect_protocol_status(
        "bad hash finish",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
            sequence++,
            NULL,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
        UPDATE_PROTOCOL_STATUS_VERIFY_ERROR
    );
    expect_metadata_status(
        "bad hash metadata readable",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &metadata, NULL)
    );
    expect_u32(
        "bad hash not candidate-ready",
        1U,
        (metadata.state != BOOT_METADATA_STATE_CANDIDATE_READY) ? 1U : 0U
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        5U,
        active_package,
        sizeof(active_package)
    );
    (void)build_update_package_for_slot(
        BOOT_SLOT_B,
        5U,
        TEST_UPDATE_PAYLOAD_SIZE,
        rollback_package,
        sizeof(rollback_package)
    );
    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "same version rollback",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            0U,
            rollback_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_ROLLBACK_ERROR
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        5U,
        active_package,
        sizeof(active_package)
    );
    (void)build_update_package_for_slot(
        BOOT_SLOT_B,
        4U,
        TEST_UPDATE_PAYLOAD_SIZE,
        rollback_package,
        sizeof(rollback_package)
    );
    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "lower version rollback",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            0U,
            rollback_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_ROLLBACK_ERROR
    );

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        5U,
        active_package,
        sizeof(active_package)
    );
    fault.point = UPDATE_INSTALL_FAULT_ERASE_SECTOR;
    fault.status = UPDATE_INSTALL_ERR_ERASE;
    fault.fired = 0U;
    options.fault_hook = protocol_fault_hook;
    options.fault_context = &fault;
    make_session(&session, &flash, &options, &capture);
    expect_protocol_status(
        "erase fault begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            0U,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_FLASH_ERROR
    );
    expect_u32("erase fault hook fired", 1U, fault.fired);
}

static void test_complete_update_flow(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *active = NULL;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t active_package[TEST_ACTIVE_PACKAGE_CAPACITY];
    uint8_t update_package[TEST_PACKAGE_CAPACITY];
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_protocol_session_t session;
    capture_writer_t capture;
    size_t active_size = 0U;
    size_t update_size = 0U;
    uint16_t sequence = 0U;
    boot_metadata_record_t metadata;

    make_installer_flash(&sim, &flash);
    expect_u32(
        "lookup active A",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_A, &active)
    );
    expect_u32(
        "lookup candidate B",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_B, &candidate)
    );

    active_size = build_update_package_for_slot(
        BOOT_SLOT_A,
        2U,
        TEST_ACTIVE_PAYLOAD_SIZE,
        active_package,
        sizeof(active_package)
    );
    install_package_bytes_direct(&sim, active, active_package, active_size);
    commit_confirmed_metadata(&flash, BOOT_SLOT_A, 2U);

    update_size = build_update_package_for_slot(
        BOOT_SLOT_B,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        update_package,
        sizeof(update_package)
    );
    make_session(&session, &flash, &options, &capture);

    expect_protocol_status(
        "update hello",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command_via_reader(
            &session,
            UPDATE_PROTOCOL_CMD_HELLO,
            sequence++,
            NULL,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );

    expect_protocol_status(
        "update begin",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command_via_reader(
            &session,
            UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
            sequence++,
            update_package,
            SIGNED_IMAGE_HEADER_SIZE
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );

    send_payload_in_chunks(
        &session,
        &capture,
        &sequence,
        &update_package[SIGNED_IMAGE_HEADER_SIZE],
        TEST_UPDATE_PAYLOAD_SIZE,
        UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE,
        1U
    );

    expect_protocol_status(
        "update finish",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command_via_reader(
            &session,
            UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
            sequence++,
            NULL,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
        UPDATE_PROTOCOL_STATUS_OK
    );

    expect_protocol_status(
        "status after update",
        UPDATE_PROTOCOL_STATUS_OK,
        send_command_via_reader(
            &session,
            UPDATE_PROTOCOL_CMD_GET_STATUS,
            sequence++,
            NULL,
            0U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_GET_STATUS,
        UPDATE_PROTOCOL_STATUS_OK
    );

    expect_metadata_status(
        "metadata after protocol update",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &metadata, NULL)
    );
    expect_u32(
        "protocol update candidate ready",
        BOOT_METADATA_STATE_CANDIDATE_READY,
        metadata.state
    );
    expect_u32("protocol update active A", BOOT_SLOT_A, metadata.active_slot);
    expect_u32("protocol update candidate B", BOOT_SLOT_B, metadata.candidate_slot);
    expect_u32("protocol update version", 3U, metadata.candidate_image_version);
    expect_u32(
        "protocol update bytes",
        TEST_UPDATE_PAYLOAD_SIZE,
        (uint32_t)session.update_payload_offset
    );
    expect_protocol_status(
        "write after finish",
        UPDATE_PROTOCOL_STATUS_OK,
        send_write_block(
            &session,
            sequence++,
            TEST_UPDATE_PAYLOAD_SIZE,
            &update_package[SIGNED_IMAGE_HEADER_SIZE],
            1U,
            1U
        )
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_NACK,
        UPDATE_PROTOCOL_CMD_WRITE_BLOCK,
        UPDATE_PROTOCOL_STATUS_STATE_ERROR
    );
    expect_u32(
        "protocol candidate bytes",
        0U,
        memcmp(
            &sim.storage[flash_offset(candidate->signed_image_base)],
            update_package,
            update_size
        )
    );
}

static void test_update_service_entry_window_limits(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_service_t service;
    update_service_config_t config;
    capture_writer_t capture;
    reset_capture_t reset;
    array_reader_t array_reader;
    byte_reader_t reader;
    const uint8_t noise[] = {
        'x', 'y', 'z', 'S', 'x', 'S', 'U', 'x'
    };
    uint8_t get_info[UPDATE_PROTOCOL_MAX_FRAME_SIZE];
    size_t get_info_size = 0U;

    make_installer_flash(&sim, &flash);

    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    array_reader.data = NULL;
    array_reader.length = 0U;
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    make_service_config(&config, &flash, &options, &reader, &capture, &reset);
    expect_u32(
        "service no data boots",
        UPDATE_SERVICE_RESULT_BOOT_CONTINUE,
        update_service_run(&service, &config)
    );
    expect_size("service no data output", 0U, capture.length);
    expect_u32("service no data reset", 0U, reset.count);

    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    array_reader.data = noise;
    array_reader.length = sizeof(noise);
    array_reader.offset = 0U;
    config.entry_max_bytes = 5U;
    expect_u32(
        "service noise bounded",
        UPDATE_SERVICE_RESULT_BOOT_CONTINUE,
        update_service_run(&service, &config)
    );
    expect_size("service noise output", 0U, capture.length);
    expect_u32("service noise reset", 0U, reset.count);

    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    get_info_size = make_frame(
        UPDATE_PROTOCOL_CMD_GET_INFO,
        0U,
        NULL,
        0U,
        get_info
    );
    array_reader.data = get_info;
    array_reader.length = get_info_size;
    array_reader.offset = 0U;
    config.entry_max_bytes = UPDATE_PROTOCOL_MAX_FRAME_SIZE;
    expect_u32(
        "service non-hello frame boots",
        UPDATE_SERVICE_RESULT_BOOT_CONTINUE,
        update_service_run(&service, &config)
    );
    expect_size("service non-hello no output", 0U, capture.length);
    expect_u32("service non-hello reset", 0U, reset.count);
}

static void test_update_service_hello_then_timeout_boots(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t stream[UPDATE_PROTOCOL_MAX_FRAME_SIZE * 3U];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_service_t service;
    update_service_config_t config;
    capture_writer_t capture;
    reset_capture_t reset;
    array_reader_t array_reader;
    byte_reader_t reader;
    size_t stream_size = 0U;

    make_installer_flash(&sim, &flash);
    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_HELLO,
        0U,
        NULL,
        0U
    );
    for (uint32_t i = 0U;
         i < ((UPDATE_PROTOCOL_MAX_FRAME_SIZE * 2U) + 4U);
         ++i) {
        stream[stream_size++] = 0xA5U;
    }
    array_reader.data = stream;
    array_reader.length = stream_size;
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    make_service_config(&config, &flash, &options, &reader, &capture, &reset);

    expect_u32(
        "service hello timeout boots",
        UPDATE_SERVICE_RESULT_BOOT_CONTINUE,
        update_service_run(&service, &config)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_u32("service hello timeout reset", 0U, reset.count);
}

static void test_update_service_after_hello_entry(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_service_t service;
    update_service_config_t config;
    capture_writer_t capture;
    reset_capture_t reset;
    array_reader_t array_reader;
    byte_reader_t reader;

    make_installer_flash(&sim, &flash);
    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    array_reader.data = NULL;
    array_reader.length = 0U;
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    make_service_config(&config, &flash, &options, &reader, &capture, &reset);

    expect_u32(
        "service after hello timeout boots",
        UPDATE_SERVICE_RESULT_BOOT_CONTINUE,
        update_service_run_after_hello(&service, &config)
    );
    expect_single_response(
        &capture,
        UPDATE_PROTOCOL_CMD_ACK,
        UPDATE_PROTOCOL_CMD_HELLO,
        UPDATE_PROTOCOL_STATUS_OK
    );
    expect_u32(
        "service after hello expected seq",
        1U,
        service.protocol.expected_sequence
    );
    expect_u32("service after hello no reset", 0U, reset.count);
}

static void test_update_service_complete_update_requests_reset(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    const boot_slot_descriptor_t *candidate = NULL;
    uint8_t active_package[TEST_ACTIVE_PACKAGE_CAPACITY];
    uint8_t update_package[TEST_PACKAGE_CAPACITY];
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t stream[TEST_CAPTURE_CAPACITY];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_service_t service;
    update_service_config_t config;
    capture_writer_t capture;
    reset_capture_t reset;
    array_reader_t array_reader;
    byte_reader_t reader;
    boot_metadata_record_t metadata;
    update_protocol_frame_t last_response;
    size_t stream_size = 0U;
    uint16_t sequence = 0U;

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    (void)build_update_package_for_slot(
        BOOT_SLOT_B,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        update_package,
        sizeof(update_package)
    );
    expect_u32(
        "service candidate lookup",
        BOOT_SLOT_LOOKUP_OK,
        boot_slot_lookup(BOOT_SLOT_B, &candidate)
    );

    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_HELLO,
        sequence++,
        NULL,
        0U
    );
    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        sequence++,
        update_package,
        SIGNED_IMAGE_HEADER_SIZE
    );
    append_payload_chunks_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        &sequence,
        &update_package[SIGNED_IMAGE_HEADER_SIZE],
        TEST_UPDATE_PAYLOAD_SIZE,
        UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE
    );
    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
        sequence++,
        NULL,
        0U
    );

    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    array_reader.data = stream;
    array_reader.length = stream_size;
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    make_service_config(&config, &flash, &options, &reader, &capture, &reset);

    expect_u32(
        "service update reset requested",
        UPDATE_SERVICE_RESULT_RESET_REQUESTED,
        update_service_run(&service, &config)
    );
    expect_u32("service reset count", 1U, reset.count);
    expect_metadata_status(
        "service update metadata",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &metadata, NULL)
    );
    expect_u32(
        "service update candidate ready",
        BOOT_METADATA_STATE_CANDIDATE_READY,
        metadata.state
    );
    expect_u32("service update candidate slot", BOOT_SLOT_B, metadata.candidate_slot);
    if (candidate != NULL) {
        expect_u32(
            "service candidate programmed",
            0U,
            memcmp(
                &sim.storage[flash_offset(candidate->signed_image_base)],
                update_package,
                SIGNED_IMAGE_HEADER_SIZE + TEST_UPDATE_PAYLOAD_SIZE
            )
        );
    }

    parse_last_response(&capture, &last_response);
    expect_protocol_status(
        "service final response status",
        UPDATE_PROTOCOL_STATUS_OK,
        response_status(
            &last_response,
            UPDATE_PROTOCOL_CMD_ACK,
            UPDATE_PROTOCOL_CMD_FINISH_UPDATE
        )
    );
    expect_u32("service final status payload", 21U, last_response.payload_length);
}

static void test_update_service_final_ack_io_error_still_resets(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t active_package[TEST_ACTIVE_PACKAGE_CAPACITY];
    uint8_t update_package[TEST_PACKAGE_CAPACITY];
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t stream[TEST_CAPTURE_CAPACITY];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_service_t service;
    update_service_config_t config;
    capture_writer_t capture;
    reset_capture_t reset;
    array_reader_t array_reader;
    byte_reader_t reader;
    boot_metadata_record_t metadata;
    size_t stream_size = 0U;
    uint16_t sequence = 0U;
    const uint32_t payload_frames =
        (TEST_UPDATE_PAYLOAD_SIZE + UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE - 1U) /
        UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE;

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    (void)build_update_package_for_slot(
        BOOT_SLOT_B,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        update_package,
        sizeof(update_package)
    );

    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_HELLO,
        sequence++,
        NULL,
        0U
    );
    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        sequence++,
        update_package,
        SIGNED_IMAGE_HEADER_SIZE
    );
    append_payload_chunks_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        &sequence,
        &update_package[SIGNED_IMAGE_HEADER_SIZE],
        TEST_UPDATE_PAYLOAD_SIZE,
        UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE
    );
    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
        sequence++,
        NULL,
        0U
    );

    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    /*
     * Each response frame uses three writer calls: header, payload, CRC.
     * Fail on the header write of the FINISH_UPDATE ACK, after the installer
     * has already committed CANDIDATE_READY.
     */
    capture.fail_on_write_call = ((1UL + 1UL + payload_frames) * 3UL) + 1UL;
    array_reader.data = stream;
    array_reader.length = stream_size;
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    make_service_config(&config, &flash, &options, &reader, &capture, &reset);

    expect_u32(
        "service final ack io reset requested",
        UPDATE_SERVICE_RESULT_RESET_REQUESTED,
        update_service_run(&service, &config)
    );
    expect_u32("service final ack io reset count", 1U, reset.count);
    expect_metadata_status(
        "service final ack io metadata",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &metadata, NULL)
    );
    expect_u32(
        "service final ack io candidate ready",
        BOOT_METADATA_STATE_CANDIDATE_READY,
        metadata.state
    );
}

static void test_update_service_failed_update_does_not_reset(void)
{
    simulated_flash_t sim;
    boot_flash_t flash;
    uint8_t active_package[TEST_ACTIVE_PACKAGE_CAPACITY];
    uint8_t update_package[TEST_PACKAGE_CAPACITY];
    uint8_t program_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t readback_buffer[TEST_PROGRAM_BUFFER_SIZE];
    uint8_t stream[TEST_CAPTURE_CAPACITY];
    update_install_options_t options =
        make_install_options(program_buffer, readback_buffer);
    update_service_t service;
    update_service_config_t config;
    capture_writer_t capture;
    reset_capture_t reset;
    array_reader_t array_reader;
    byte_reader_t reader;
    boot_metadata_record_t metadata;
    update_protocol_frame_t last_response;
    size_t stream_size = 0U;
    uint16_t sequence = 0U;

    make_installer_flash(&sim, &flash);
    prepare_confirmed_active_image(
        &sim,
        &flash,
        BOOT_SLOT_A,
        2U,
        active_package,
        sizeof(active_package)
    );
    (void)build_update_package_for_slot(
        BOOT_SLOT_B,
        3U,
        TEST_UPDATE_PAYLOAD_SIZE,
        update_package,
        sizeof(update_package)
    );
    update_package[SIGNED_IMAGE_HEADER_SIZE + 31U] ^= 0x01U;

    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_HELLO,
        sequence++,
        NULL,
        0U
    );
    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_BEGIN_UPDATE,
        sequence++,
        update_package,
        SIGNED_IMAGE_HEADER_SIZE
    );
    append_payload_chunks_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        &sequence,
        &update_package[SIGNED_IMAGE_HEADER_SIZE],
        TEST_UPDATE_PAYLOAD_SIZE,
        256U
    );
    append_frame_to_stream(
        stream,
        sizeof(stream),
        &stream_size,
        UPDATE_PROTOCOL_CMD_FINISH_UPDATE,
        sequence++,
        NULL,
        0U
    );

    memset(&capture, 0, sizeof(capture));
    memset(&reset, 0, sizeof(reset));
    array_reader.data = stream;
    array_reader.length = stream_size;
    array_reader.offset = 0U;
    byte_reader_init(&reader, array_getc, &array_reader);
    make_service_config(&config, &flash, &options, &reader, &capture, &reset);

    expect_u32(
        "service bad hash boots",
        UPDATE_SERVICE_RESULT_BOOT_CONTINUE,
        update_service_run(&service, &config)
    );
    expect_u32("service bad hash no reset", 0U, reset.count);
    expect_metadata_status(
        "service bad hash metadata",
        BOOT_METADATA_OK,
        boot_metadata_recover_from_flash(&flash, &metadata, NULL)
    );
    expect_u32(
        "service bad hash not candidate ready",
        1U,
        (metadata.state != BOOT_METADATA_STATE_CANDIDATE_READY) ? 1U : 0U
    );

    parse_last_response(&capture, &last_response);
    expect_protocol_status(
        "service bad hash final nack",
        UPDATE_PROTOCOL_STATUS_VERIFY_ERROR,
        response_status(
            &last_response,
            UPDATE_PROTOCOL_CMD_NACK,
            UPDATE_PROTOCOL_CMD_FINISH_UPDATE
        )
    );
}

int main(void)
{
    init_update_keys();
    test_parser_byte_by_byte_and_fragments();
    test_session_fragmented_frame_input();
    test_session_multiple_frames_and_resync();
    test_invalid_frames_fail_closed();
    test_sequence_and_timeout_errors();
    test_update_command_state_errors();
    test_update_verify_rollback_and_flash_errors();
    test_complete_update_flow();
    test_update_service_entry_window_limits();
    test_update_service_hello_then_timeout_boots();
    test_update_service_after_hello_entry();
    test_update_service_complete_update_requests_reset();
    test_update_service_final_ack_io_error_still_resets();
    test_update_service_failed_update_does_not_reset();

    if (failures != 0) {
        printf("update protocol tests failed: %d\n", failures);
        return 1;
    }

    printf("update protocol tests passed\n");
    return 0;
}
