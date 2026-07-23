#include "update_protocol.h"

#include <string.h>

#include "boot_slot.h"

#define UPDATE_PROTOCOL_STATE_SYNC       0U
#define UPDATE_PROTOCOL_STATE_HEADER     1U
#define UPDATE_PROTOCOL_STATE_PAYLOAD    2U
#define UPDATE_PROTOCOL_STATE_CRC        3U

#define UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE 3U
#define UPDATE_PROTOCOL_INFO_PAYLOAD_SIZE    10U
#define UPDATE_PROTOCOL_STATUS_PAYLOAD_SIZE  21U
#define UPDATE_PROTOCOL_READER_MAX_BYTES \
    (UPDATE_PROTOCOL_MAX_FRAME_SIZE * 2U)

static const uint8_t protocol_magic[UPDATE_PROTOCOL_MAGIC_SIZE] = {
    'S', 'U', 'P', 'D'
};

static uint16_t load_le16(const uint8_t bytes[2])
{
    return (uint16_t)(((uint16_t)bytes[0]) |
        ((uint16_t)bytes[1] << 8));
}

static uint32_t load_le32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0]) |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
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

static void parser_reset_state(update_protocol_parser_t *parser)
{
    parser->state = UPDATE_PROTOCOL_STATE_SYNC;
    parser->magic_matched = 0U;
    parser->header_decoded = 0U;
    parser->header_received = 0U;
    parser->payload_received = 0U;
    parser->crc_received = 0U;
}

void update_protocol_parser_init(update_protocol_parser_t *parser)
{
    if (parser != NULL) {
        memset(parser, 0, sizeof(*parser));
        parser_reset_state(parser);
    }
}

static update_protocol_parse_status_t parser_decode_header(
    update_protocol_parser_t *parser
)
{
    parser->frame.version = parser->header[4];
    parser->frame.command = parser->header[5];
    parser->frame.sequence = load_le16(&parser->header[6]);
    parser->frame.payload_length = load_le16(&parser->header[8]);
    parser->error_command = parser->frame.command;
    parser->error_sequence = parser->frame.sequence;
    parser->error_info_valid = 1U;
    parser->header_decoded = 1U;

    if (parser->frame.payload_length > UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE) {
        parser_reset_state(parser);
        return UPDATE_PROTOCOL_PARSE_ERR_OVERSIZE;
    }

    if (parser->frame.payload_length == 0U) {
        parser->state = UPDATE_PROTOCOL_STATE_CRC;
    } else {
        parser->state = UPDATE_PROTOCOL_STATE_PAYLOAD;
    }

    return UPDATE_PROTOCOL_PARSE_NEED_MORE;
}

static update_protocol_parse_status_t parser_accept_magic_byte(
    update_protocol_parser_t *parser,
    uint8_t byte
)
{
    if (byte == protocol_magic[parser->magic_matched]) {
        if (parser->magic_matched == 0U) {
            parser->error_info_valid = 0U;
        }
        parser->header[parser->magic_matched] = byte;
        ++parser->magic_matched;
        parser->header_received = parser->magic_matched;

        if (parser->magic_matched == UPDATE_PROTOCOL_MAGIC_SIZE) {
            parser->state = UPDATE_PROTOCOL_STATE_HEADER;
        }
        return UPDATE_PROTOCOL_PARSE_NEED_MORE;
    }

    if (byte == protocol_magic[0]) {
        parser->error_info_valid = 0U;
        parser->header[0] = byte;
        parser->magic_matched = 1U;
        parser->header_received = 1U;
    } else {
        parser->magic_matched = 0U;
        parser->header_received = 0U;
    }

    return UPDATE_PROTOCOL_PARSE_NEED_MORE;
}

static update_protocol_parse_status_t parser_finish_frame(
    update_protocol_parser_t *parser
)
{
    const uint32_t expected_crc = load_le32(parser->crc_bytes);
    uint32_t actual_crc = update_protocol_crc32_update(
        0U,
        parser->header,
        sizeof(parser->header)
    );

    actual_crc = update_protocol_crc32_update(
        actual_crc,
        parser->frame.payload,
        parser->frame.payload_length
    );

    parser_reset_state(parser);
    return (actual_crc == expected_crc)
        ? UPDATE_PROTOCOL_PARSE_FRAME_READY
        : UPDATE_PROTOCOL_PARSE_ERR_CRC;
}

update_protocol_parse_status_t update_protocol_parser_push(
    update_protocol_parser_t *parser,
    uint8_t byte
)
{
    if (parser == NULL) {
        return UPDATE_PROTOCOL_PARSE_ERR_INVALID_ARGUMENT;
    }

    switch (parser->state) {
    case UPDATE_PROTOCOL_STATE_SYNC:
        return parser_accept_magic_byte(parser, byte);

    case UPDATE_PROTOCOL_STATE_HEADER:
        parser->header[parser->header_received] = byte;
        ++parser->header_received;
        if (parser->header_received == UPDATE_PROTOCOL_HEADER_SIZE) {
            return parser_decode_header(parser);
        }
        return UPDATE_PROTOCOL_PARSE_NEED_MORE;

    case UPDATE_PROTOCOL_STATE_PAYLOAD:
        parser->frame.payload[parser->payload_received] = byte;
        ++parser->payload_received;
        if (parser->payload_received == parser->frame.payload_length) {
            parser->state = UPDATE_PROTOCOL_STATE_CRC;
        }
        return UPDATE_PROTOCOL_PARSE_NEED_MORE;

    case UPDATE_PROTOCOL_STATE_CRC:
        parser->crc_bytes[parser->crc_received] = byte;
        ++parser->crc_received;
        if (parser->crc_received == UPDATE_PROTOCOL_CRC_SIZE) {
            return parser_finish_frame(parser);
        }
        return UPDATE_PROTOCOL_PARSE_NEED_MORE;

    default:
        parser_reset_state(parser);
        return UPDATE_PROTOCOL_PARSE_ERR_INVALID_ARGUMENT;
    }
}

const update_protocol_frame_t *update_protocol_parser_frame(
    const update_protocol_parser_t *parser
)
{
    return (parser != NULL) ? &parser->frame : NULL;
}

uint8_t update_protocol_parser_has_partial(
    const update_protocol_parser_t *parser
)
{
    if (parser == NULL) {
        return 0U;
    }

    return (parser->state != UPDATE_PROTOCOL_STATE_SYNC) ? 1U : 0U;
}

uint8_t update_protocol_parser_error_info(
    const update_protocol_parser_t *parser,
    uint8_t *command,
    uint16_t *sequence
)
{
    if ((parser == NULL) || (parser->error_info_valid == 0U)) {
        return 0U;
    }

    if (command != NULL) {
        *command = parser->error_command;
    }
    if (sequence != NULL) {
        *sequence = parser->error_sequence;
    }

    return 1U;
}

uint32_t update_protocol_crc32_update(
    uint32_t crc,
    const uint8_t *data,
    size_t length
)
{
    crc = ~crc;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0UL - (crc & 1UL);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

uint32_t update_protocol_crc32(const uint8_t *data, size_t length)
{
    return update_protocol_crc32_update(0U, data, length);
}

static update_protocol_status_t build_header(
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t header[UPDATE_PROTOCOL_HEADER_SIZE],
    uint32_t *crc
)
{
    if ((header == NULL) ||
        (crc == NULL) ||
        ((payload_length != 0U) && (payload == NULL)) ||
        (payload_length > UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    memcpy(header, protocol_magic, sizeof(protocol_magic));
    header[4] = UPDATE_PROTOCOL_VERSION;
    header[5] = command;
    store_le16(&header[6], sequence);
    store_le16(&header[8], payload_length);

    *crc = update_protocol_crc32_update(0U, header, UPDATE_PROTOCOL_HEADER_SIZE);
    *crc = update_protocol_crc32_update(*crc, payload, payload_length);
    return UPDATE_PROTOCOL_STATUS_OK;
}

update_protocol_status_t update_protocol_encode_frame(
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *frame_size
)
{
    uint8_t header[UPDATE_PROTOCOL_HEADER_SIZE];
    uint32_t crc = 0U;
    const size_t total_size =
        (size_t)UPDATE_PROTOCOL_HEADER_SIZE +
        (size_t)payload_length +
        (size_t)UPDATE_PROTOCOL_CRC_SIZE;

    if (frame_size != NULL) {
        *frame_size = 0U;
    }

    if ((output == NULL) || (output_capacity < total_size)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    update_protocol_status_t status =
        build_header(command, sequence, payload, payload_length, header, &crc);
    if (status != UPDATE_PROTOCOL_STATUS_OK) {
        return status;
    }

    memcpy(output, header, sizeof(header));
    if (payload_length != 0U) {
        memcpy(&output[UPDATE_PROTOCOL_HEADER_SIZE], payload, payload_length);
    }
    store_le32(
        &output[UPDATE_PROTOCOL_HEADER_SIZE + payload_length],
        crc
    );

    if (frame_size != NULL) {
        *frame_size = total_size;
    }
    return UPDATE_PROTOCOL_STATUS_OK;
}

update_protocol_status_t update_protocol_write_frame(
    const update_protocol_writer_t *writer,
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length
)
{
    uint8_t header[UPDATE_PROTOCOL_HEADER_SIZE];
    uint8_t crc_bytes[UPDATE_PROTOCOL_CRC_SIZE];
    uint32_t crc = 0U;

    if ((writer == NULL) || (writer->write == NULL)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    update_protocol_status_t status =
        build_header(command, sequence, payload, payload_length, header, &crc);
    if (status != UPDATE_PROTOCOL_STATUS_OK) {
        return status;
    }

    store_le32(crc_bytes, crc);

    status = writer->write(writer->context, header, sizeof(header));
    if (status != UPDATE_PROTOCOL_STATUS_OK) {
        return status;
    }
    if (payload_length != 0U) {
        status = writer->write(writer->context, payload, payload_length);
        if (status != UPDATE_PROTOCOL_STATUS_OK) {
            return status;
        }
    }
    return writer->write(writer->context, crc_bytes, sizeof(crc_bytes));
}

static void response_common(
    uint8_t payload[UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE],
    uint8_t request_command,
    update_protocol_status_t status
)
{
    payload[0] = request_command;
    store_le16(&payload[1], (uint16_t)status);
}

static update_protocol_status_t send_response(
    update_protocol_session_t *session,
    uint8_t response_command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length
)
{
    if (session == NULL) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    return update_protocol_write_frame(
        &session->writer,
        response_command,
        sequence,
        payload,
        payload_length
    );
}

static update_protocol_status_t send_nack(
    update_protocol_session_t *session,
    uint16_t sequence,
    uint8_t request_command,
    update_protocol_status_t status
)
{
    uint8_t payload[UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE];

    if (status == UPDATE_PROTOCOL_STATUS_OK) {
        status = UPDATE_PROTOCOL_STATUS_PARSER_ERROR;
    }
    response_common(payload, request_command, status);
    if (session != NULL) {
        session->last_status = status;
    }
    return send_response(
        session,
        UPDATE_PROTOCOL_CMD_NACK,
        sequence,
        payload,
        sizeof(payload)
    );
}

static update_protocol_status_t send_ack(
    update_protocol_session_t *session,
    uint16_t sequence,
    uint8_t request_command,
    const uint8_t *extra,
    uint16_t extra_length
)
{
    uint8_t payload[UPDATE_PROTOCOL_STATUS_PAYLOAD_SIZE];

    if (extra_length >
        (sizeof(payload) - UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    response_common(payload, request_command, UPDATE_PROTOCOL_STATUS_OK);
    if (extra_length != 0U) {
        memcpy(
            &payload[UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE],
            extra,
            extra_length
        );
    }
    if (session != NULL) {
        session->last_status = UPDATE_PROTOCOL_STATUS_OK;
    }
    return send_response(
        session,
        UPDATE_PROTOCOL_CMD_ACK,
        sequence,
        payload,
        (uint16_t)(UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE + extra_length)
    );
}

static update_protocol_status_t map_installer_status(
    update_install_status_t status
)
{
    switch (status) {
    case UPDATE_INSTALL_OK:
        return UPDATE_PROTOCOL_STATUS_OK;
    case UPDATE_INSTALL_ERR_ROLLBACK:
        return UPDATE_PROTOCOL_STATUS_ROLLBACK_ERROR;
    case UPDATE_INSTALL_ERR_PACKAGE:
    case UPDATE_INSTALL_ERR_HASH:
    case UPDATE_INSTALL_ERR_INSTALLED_VERIFY:
        return UPDATE_PROTOCOL_STATUS_VERIFY_ERROR;
    case UPDATE_INSTALL_ERR_METADATA:
    case UPDATE_INSTALL_ERR_SLOT:
    case UPDATE_INSTALL_ERR_CAPACITY:
    case UPDATE_INSTALL_ERR_ERASE:
    case UPDATE_INSTALL_ERR_PROGRAM:
    case UPDATE_INSTALL_ERR_READBACK:
        return UPDATE_PROTOCOL_STATUS_FLASH_ERROR;
    case UPDATE_INSTALL_ERR_ACTIVE_STATE:
    case UPDATE_INSTALL_ERR_STATE:
        return UPDATE_PROTOCOL_STATUS_STATE_ERROR;
    case UPDATE_INSTALL_ERR_SEQUENCE:
        return UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR;
    case UPDATE_INSTALL_ERR_INVALID_ARGUMENT:
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    case UPDATE_INSTALL_ERR_INJECTED:
    default:
        return UPDATE_PROTOCOL_STATUS_INSTALLER_ERROR;
    }
}

static uint8_t payload_is_empty(const update_protocol_frame_t *frame)
{
    return ((frame != NULL) && (frame->payload_length == 0U)) ? 1U : 0U;
}

static update_protocol_status_t send_hello_ack(
    update_protocol_session_t *session,
    uint16_t sequence,
    uint8_t command
);

static update_protocol_status_t handle_hello(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    if (payload_is_empty(frame) == 0U) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
        );
    }

    return send_hello_ack(session, frame->sequence, frame->command);
}

static update_protocol_status_t send_hello_ack(
    update_protocol_session_t *session,
    uint16_t sequence,
    uint8_t command
)
{
    uint8_t info[UPDATE_PROTOCOL_INFO_PAYLOAD_SIZE];

    info[0] = UPDATE_PROTOCOL_VERSION;
    store_le16(&info[1], UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE);
    store_le16(&info[3], UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE);
    store_le16(&info[5], UPDATE_PROTOCOL_HEADER_SIZE);
    store_le16(&info[7], UPDATE_PROTOCOL_CRC_SIZE);
    info[9] = (uint8_t)session->state;
    return send_ack(session, sequence, command, info, sizeof(info));
}

static update_protocol_status_t handle_get_info(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    return handle_hello(session, frame);
}

static void build_status_payload(
    const update_protocol_session_t *session,
    uint8_t payload[UPDATE_PROTOCOL_STATUS_PAYLOAD_SIZE -
        UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE]
);

static update_protocol_status_t handle_get_status(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    uint8_t payload[UPDATE_PROTOCOL_STATUS_PAYLOAD_SIZE -
        UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE];

    if (payload_is_empty(frame) == 0U) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
        );
    }

    build_status_payload(session, payload);

    return send_ack(
        session,
        frame->sequence,
        frame->command,
        payload,
        sizeof(payload)
    );
}

static void build_status_payload(
    const update_protocol_session_t *session,
    uint8_t payload[UPDATE_PROTOCOL_STATUS_PAYLOAD_SIZE -
        UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE]
)
{
    payload[0] = (uint8_t)session->state;
    store_le16(&payload[1], (uint16_t)session->last_status);
    store_le16(&payload[3], session->expected_sequence);
    payload[5] = (uint8_t)session->installer.state;
    store_le32(&payload[6], (uint32_t)session->update_payload_offset);
    store_le32(&payload[10], session->install_result.image_version);
    store_le32(&payload[14], session->install_result.programmed_block_count);
}

static update_protocol_status_t handle_begin_update(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    update_install_status_t install_status;
    update_protocol_status_t protocol_status;

    if (frame->payload_length != (uint16_t)SIGNED_IMAGE_HEADER_SIZE) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
        );
    }

    if ((session->state != UPDATE_PROTOCOL_SESSION_IDLE) &&
        (session->state != UPDATE_PROTOCOL_SESSION_ABORTED)) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_STATE_ERROR
        );
    }

    install_status = update_installer_session_init(
        &session->installer,
        session->flash,
        session->public_key,
        &session->install_options,
        &session->install_result
    );
    protocol_status = map_installer_status(install_status);
    if (protocol_status == UPDATE_PROTOCOL_STATUS_OK) {
        session->installer_started = 1U;
        install_status = update_installer_begin(
            &session->installer,
            frame->payload,
            frame->payload_length
        );
        protocol_status = map_installer_status(install_status);
    }

    if (protocol_status != UPDATE_PROTOCOL_STATUS_OK) {
        if (session->installer_started != 0U) {
            (void)update_installer_abort(&session->installer);
        }
        session->state = UPDATE_PROTOCOL_SESSION_FAILED;
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            protocol_status
        );
    }

    session->state = UPDATE_PROTOCOL_SESSION_UPDATING;
    session->update_payload_offset = 0U;
    return send_ack(session, frame->sequence, frame->command, NULL, 0U);
}

static update_protocol_status_t handle_write_block(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    uint32_t payload_offset = 0U;
    size_t block_length = 0U;
    update_install_status_t install_status;
    update_protocol_status_t protocol_status;

    if ((session->state != UPDATE_PROTOCOL_SESSION_UPDATING) ||
        (session->installer_started == 0U)) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_STATE_ERROR
        );
    }

    if ((frame->payload_length <= UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE) ||
        (frame->payload_length > UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE)) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
        );
    }

    payload_offset = load_le32(frame->payload);
    block_length =
        (size_t)frame->payload_length - UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE;
    if (payload_offset != session->update_payload_offset) {
        session->state = UPDATE_PROTOCOL_SESSION_FAILED;
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
        );
    }

    install_status = update_installer_write(
        &session->installer,
        (size_t)payload_offset,
        &frame->payload[UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE],
        block_length
    );
    protocol_status = map_installer_status(install_status);
    if (protocol_status != UPDATE_PROTOCOL_STATUS_OK) {
        session->state = UPDATE_PROTOCOL_SESSION_FAILED;
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            protocol_status
        );
    }

    session->update_payload_offset += block_length;
    return send_ack(session, frame->sequence, frame->command, NULL, 0U);
}

static update_protocol_status_t handle_finish_update(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    uint8_t payload[UPDATE_PROTOCOL_STATUS_PAYLOAD_SIZE -
        UPDATE_PROTOCOL_RESPONSE_COMMON_SIZE];
    update_install_status_t install_status;
    update_protocol_status_t protocol_status;

    if (payload_is_empty(frame) == 0U) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
        );
    }

    if ((session->state != UPDATE_PROTOCOL_SESSION_UPDATING) ||
        (session->installer_started == 0U)) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_STATE_ERROR
        );
    }

    install_status = update_installer_finish(&session->installer);
    protocol_status = map_installer_status(install_status);
    if (protocol_status != UPDATE_PROTOCOL_STATUS_OK) {
        session->state = UPDATE_PROTOCOL_SESSION_FAILED;
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            protocol_status
        );
    }

    session->state = UPDATE_PROTOCOL_SESSION_FINISHED;
    build_status_payload(session, payload);
    return send_ack(session, frame->sequence, frame->command, payload, sizeof(payload));
}

static update_protocol_status_t handle_abort_update(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    update_install_status_t install_status = UPDATE_INSTALL_OK;
    update_protocol_status_t protocol_status = UPDATE_PROTOCOL_STATUS_OK;

    if (payload_is_empty(frame) == 0U) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
        );
    }

    if (session->state == UPDATE_PROTOCOL_SESSION_FINISHED) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_STATE_ERROR
        );
    }

    if (session->installer_started != 0U) {
        install_status = update_installer_abort(&session->installer);
        protocol_status = map_installer_status(install_status);
    }
    if (protocol_status != UPDATE_PROTOCOL_STATUS_OK) {
        session->state = UPDATE_PROTOCOL_SESSION_FAILED;
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            protocol_status
        );
    }

    session->state = UPDATE_PROTOCOL_SESSION_ABORTED;
    return send_ack(session, frame->sequence, frame->command, NULL, 0U);
}

static update_protocol_status_t handle_reset(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    if (payload_is_empty(frame) == 0U) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_LENGTH_ERROR
        );
    }

    session->state = UPDATE_PROTOCOL_SESSION_RESET_REQUESTED;
    return send_ack(session, frame->sequence, frame->command, NULL, 0U);
}

static update_protocol_status_t execute_frame(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    switch ((update_protocol_command_t)frame->command) {
    case UPDATE_PROTOCOL_CMD_HELLO:
        return handle_hello(session, frame);
    case UPDATE_PROTOCOL_CMD_GET_INFO:
        return handle_get_info(session, frame);
    case UPDATE_PROTOCOL_CMD_BEGIN_UPDATE:
        return handle_begin_update(session, frame);
    case UPDATE_PROTOCOL_CMD_WRITE_BLOCK:
        return handle_write_block(session, frame);
    case UPDATE_PROTOCOL_CMD_FINISH_UPDATE:
        return handle_finish_update(session, frame);
    case UPDATE_PROTOCOL_CMD_GET_STATUS:
        return handle_get_status(session, frame);
    case UPDATE_PROTOCOL_CMD_ABORT_UPDATE:
        return handle_abort_update(session, frame);
    case UPDATE_PROTOCOL_CMD_RESET:
        return handle_reset(session, frame);
    case UPDATE_PROTOCOL_CMD_ACK:
    case UPDATE_PROTOCOL_CMD_NACK:
    default:
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_UNKNOWN_COMMAND
        );
    }
}

static void advance_sequence(update_protocol_session_t *session)
{
    session->expected_sequence = (uint16_t)(session->expected_sequence + 1U);
}

update_protocol_status_t update_protocol_session_accept_initial_hello(
    update_protocol_session_t *session
)
{
    update_protocol_status_t status;

    if (session == NULL) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (session->expected_sequence != 0U) {
        return send_nack(
            session,
            0U,
            (uint8_t)UPDATE_PROTOCOL_CMD_HELLO,
            UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
        );
    }

    status = send_hello_ack(
        session,
        0U,
        (uint8_t)UPDATE_PROTOCOL_CMD_HELLO
    );
    if (status == UPDATE_PROTOCOL_STATUS_OK) {
        advance_sequence(session);
    }

    return status;
}

static update_protocol_status_t handle_frame(
    update_protocol_session_t *session,
    const update_protocol_frame_t *frame
)
{
    update_protocol_status_t status;

    if ((session == NULL) || (frame == NULL)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (frame->version != UPDATE_PROTOCOL_VERSION) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_VERSION_ERROR
        );
    }

    if (frame->sequence != session->expected_sequence) {
        return send_nack(
            session,
            frame->sequence,
            frame->command,
            UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR
        );
    }

    status = execute_frame(session, frame);
    advance_sequence(session);
    return status;
}

void update_protocol_session_init(
    update_protocol_session_t *session,
    const boot_flash_t *flash,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *install_options,
    update_protocol_write_fn_t write,
    void *write_context
)
{
    if (session == NULL) {
        return;
    }

    memset(session, 0, sizeof(*session));
    update_protocol_parser_init(&session->parser);
    session->writer.context = write_context;
    session->writer.write = write;
    session->flash = flash;
    session->public_key = public_key;
    if (install_options != NULL) {
        session->install_options = *install_options;
    }
    session->state = UPDATE_PROTOCOL_SESSION_IDLE;
    session->last_status = UPDATE_PROTOCOL_STATUS_OK;
    session->expected_sequence = 0U;
}

static update_protocol_status_t handle_parse_error(
    update_protocol_session_t *session,
    update_protocol_parse_status_t parse_status
)
{
    uint8_t command = 0U;
    uint16_t sequence = session->expected_sequence;
    update_protocol_status_t status = UPDATE_PROTOCOL_STATUS_PARSER_ERROR;

    switch (parse_status) {
    case UPDATE_PROTOCOL_PARSE_ERR_OVERSIZE:
        status = UPDATE_PROTOCOL_STATUS_OVERSIZE_ERROR;
        break;
    case UPDATE_PROTOCOL_PARSE_ERR_CRC:
        status = UPDATE_PROTOCOL_STATUS_CRC_ERROR;
        break;
    case UPDATE_PROTOCOL_PARSE_ERR_INVALID_ARGUMENT:
        status = UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
        break;
    default:
        status = UPDATE_PROTOCOL_STATUS_PARSER_ERROR;
        break;
    }

    (void)update_protocol_parser_error_info(
        &session->parser,
        &command,
        &sequence
    );
    return send_nack(session, sequence, command, status);
}

update_protocol_status_t update_protocol_session_process_byte(
    update_protocol_session_t *session,
    uint8_t byte
)
{
    update_protocol_parse_status_t parse_status;

    if (session == NULL) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    parse_status = update_protocol_parser_push(&session->parser, byte);
    if (parse_status == UPDATE_PROTOCOL_PARSE_NEED_MORE) {
        return UPDATE_PROTOCOL_STATUS_OK;
    }
    if (parse_status == UPDATE_PROTOCOL_PARSE_FRAME_READY) {
        return handle_frame(session, update_protocol_parser_frame(&session->parser));
    }

    return handle_parse_error(session, parse_status);
}

update_protocol_status_t update_protocol_session_process_reader(
    update_protocol_session_t *session,
    const byte_reader_t *reader,
    uint32_t byte_timeout_polls
)
{
    uint32_t byte_count = 0U;

    if ((session == NULL) || (reader == NULL)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    for (;;) {
        uint8_t byte = 0U;
        const byte_reader_status_t read_status =
            byte_reader_getc_timeout(reader, &byte, byte_timeout_polls);

        if (read_status == BYTE_READER_TIMEOUT) {
            if (update_protocol_parser_has_partial(&session->parser) != 0U) {
                uint8_t command = 0U;
                uint16_t sequence = session->expected_sequence;
                update_protocol_status_t write_status =
                    UPDATE_PROTOCOL_STATUS_OK;
                const uint8_t has_error_info = update_protocol_parser_error_info(
                    &session->parser,
                    &command,
                    &sequence
                );
                update_protocol_parser_init(&session->parser);
                session->last_status = UPDATE_PROTOCOL_STATUS_TIMEOUT;
                if (has_error_info != 0U) {
                    write_status = send_nack(
                        session,
                        sequence,
                        command,
                        UPDATE_PROTOCOL_STATUS_TIMEOUT
                    );
                    if (write_status != UPDATE_PROTOCOL_STATUS_OK) {
                        return write_status;
                    }
                }
            }
            return UPDATE_PROTOCOL_STATUS_TIMEOUT;
        }
        if (read_status != BYTE_READER_OK) {
            session->last_status = UPDATE_PROTOCOL_STATUS_IO_ERROR;
            return UPDATE_PROTOCOL_STATUS_IO_ERROR;
        }

        ++byte_count;
        if (byte_count > UPDATE_PROTOCOL_READER_MAX_BYTES) {
            update_protocol_parser_init(&session->parser);
            session->last_status = UPDATE_PROTOCOL_STATUS_TIMEOUT;
            return UPDATE_PROTOCOL_STATUS_TIMEOUT;
        }

        const update_protocol_parse_status_t parse_status =
            update_protocol_parser_push(&session->parser, byte);
        if (parse_status == UPDATE_PROTOCOL_PARSE_NEED_MORE) {
            continue;
        }
        if (parse_status == UPDATE_PROTOCOL_PARSE_FRAME_READY) {
            return handle_frame(session, update_protocol_parser_frame(&session->parser));
        }
        return handle_parse_error(session, parse_status);
    }
}

const char *update_protocol_status_text(update_protocol_status_t status)
{
    switch (status) {
    case UPDATE_PROTOCOL_STATUS_OK:               return "OK";
    case UPDATE_PROTOCOL_STATUS_PARSER_ERROR:     return "PARSER";
    case UPDATE_PROTOCOL_STATUS_STATE_ERROR:      return "STATE";
    case UPDATE_PROTOCOL_STATUS_FLASH_ERROR:      return "FLASH";
    case UPDATE_PROTOCOL_STATUS_VERIFY_ERROR:     return "VERIFY";
    case UPDATE_PROTOCOL_STATUS_ROLLBACK_ERROR:   return "ROLLBACK";
    case UPDATE_PROTOCOL_STATUS_CRC_ERROR:        return "CRC";
    case UPDATE_PROTOCOL_STATUS_VERSION_ERROR:    return "VERSION";
    case UPDATE_PROTOCOL_STATUS_LENGTH_ERROR:     return "LENGTH";
    case UPDATE_PROTOCOL_STATUS_OVERSIZE_ERROR:   return "OVERSIZE";
    case UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR:   return "SEQUENCE";
    case UPDATE_PROTOCOL_STATUS_UNKNOWN_COMMAND:  return "UNKNOWN COMMAND";
    case UPDATE_PROTOCOL_STATUS_TIMEOUT:          return "TIMEOUT";
    case UPDATE_PROTOCOL_STATUS_IO_ERROR:         return "IO";
    case UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT: return "INVALID ARGUMENT";
    case UPDATE_PROTOCOL_STATUS_INSTALLER_ERROR:  return "INSTALLER";
    default:                                      return "UNKNOWN";
    }
}

const char *update_protocol_session_state_text(update_protocol_session_state_t state)
{
    switch (state) {
    case UPDATE_PROTOCOL_SESSION_IDLE:            return "IDLE";
    case UPDATE_PROTOCOL_SESSION_UPDATING:        return "UPDATING";
    case UPDATE_PROTOCOL_SESSION_FINISHED:        return "FINISHED";
    case UPDATE_PROTOCOL_SESSION_ABORTED:         return "ABORTED";
    case UPDATE_PROTOCOL_SESSION_FAILED:          return "FAILED";
    case UPDATE_PROTOCOL_SESSION_RESET_REQUESTED: return "RESET REQUESTED";
    default:                                      return "UNKNOWN";
    }
}
