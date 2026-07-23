#ifndef UPDATE_PROTOCOL_H
#define UPDATE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "byte_reader.h"
#include "signed_image.h"
#include "update_installer.h"

#define UPDATE_PROTOCOL_MAGIC_SIZE        4U
#define UPDATE_PROTOCOL_HEADER_SIZE       10U
#define UPDATE_PROTOCOL_CRC_SIZE          4U
#define UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE  1024U
#define UPDATE_PROTOCOL_MAX_FRAME_SIZE \
    (UPDATE_PROTOCOL_HEADER_SIZE + \
     UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE + \
     UPDATE_PROTOCOL_CRC_SIZE)
#define UPDATE_PROTOCOL_VERSION           1U
#define UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE 4U
#define UPDATE_PROTOCOL_MAX_WRITE_DATA_SIZE \
    (UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE - UPDATE_PROTOCOL_WRITE_BLOCK_PREFIX_SIZE)

typedef enum {
    UPDATE_PROTOCOL_CMD_HELLO = 0x01,
    UPDATE_PROTOCOL_CMD_GET_INFO = 0x02,
    UPDATE_PROTOCOL_CMD_BEGIN_UPDATE = 0x03,
    UPDATE_PROTOCOL_CMD_WRITE_BLOCK = 0x04,
    UPDATE_PROTOCOL_CMD_FINISH_UPDATE = 0x05,
    UPDATE_PROTOCOL_CMD_GET_STATUS = 0x06,
    UPDATE_PROTOCOL_CMD_ABORT_UPDATE = 0x07,
    UPDATE_PROTOCOL_CMD_RESET = 0x08,
    UPDATE_PROTOCOL_CMD_ACK = 0x80,
    UPDATE_PROTOCOL_CMD_NACK = 0x81
} update_protocol_command_t;

typedef enum {
    UPDATE_PROTOCOL_STATUS_OK = 0,
    UPDATE_PROTOCOL_STATUS_PARSER_ERROR = 1,
    UPDATE_PROTOCOL_STATUS_STATE_ERROR = 2,
    UPDATE_PROTOCOL_STATUS_FLASH_ERROR = 3,
    UPDATE_PROTOCOL_STATUS_VERIFY_ERROR = 4,
    UPDATE_PROTOCOL_STATUS_ROLLBACK_ERROR = 5,
    UPDATE_PROTOCOL_STATUS_CRC_ERROR = 6,
    UPDATE_PROTOCOL_STATUS_VERSION_ERROR = 7,
    UPDATE_PROTOCOL_STATUS_LENGTH_ERROR = 8,
    UPDATE_PROTOCOL_STATUS_OVERSIZE_ERROR = 9,
    UPDATE_PROTOCOL_STATUS_SEQUENCE_ERROR = 10,
    UPDATE_PROTOCOL_STATUS_UNKNOWN_COMMAND = 11,
    UPDATE_PROTOCOL_STATUS_TIMEOUT = 12,
    UPDATE_PROTOCOL_STATUS_IO_ERROR = 13,
    UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT = 14,
    UPDATE_PROTOCOL_STATUS_INSTALLER_ERROR = 15
} update_protocol_status_t;

typedef enum {
    UPDATE_PROTOCOL_PARSE_NEED_MORE = 0,
    UPDATE_PROTOCOL_PARSE_FRAME_READY = 1,
    UPDATE_PROTOCOL_PARSE_ERR_OVERSIZE = 2,
    UPDATE_PROTOCOL_PARSE_ERR_CRC = 3,
    UPDATE_PROTOCOL_PARSE_ERR_INVALID_ARGUMENT = 4
} update_protocol_parse_status_t;

typedef enum {
    UPDATE_PROTOCOL_SESSION_IDLE = 0,
    UPDATE_PROTOCOL_SESSION_UPDATING = 1,
    UPDATE_PROTOCOL_SESSION_FINISHED = 2,
    UPDATE_PROTOCOL_SESSION_ABORTED = 3,
    UPDATE_PROTOCOL_SESSION_FAILED = 4,
    UPDATE_PROTOCOL_SESSION_RESET_REQUESTED = 5
} update_protocol_session_state_t;

typedef update_protocol_status_t (*update_protocol_write_fn_t)(
    void *context,
    const uint8_t *data,
    size_t length
);

typedef struct {
    void *context;
    update_protocol_write_fn_t write;
} update_protocol_writer_t;

typedef struct {
    uint8_t version;
    uint8_t command;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[UPDATE_PROTOCOL_MAX_PAYLOAD_SIZE];
} update_protocol_frame_t;

typedef struct {
    uint8_t state;
    uint8_t magic_matched;
    uint8_t header_decoded;
    size_t header_received;
    size_t payload_received;
    size_t crc_received;
    uint8_t header[UPDATE_PROTOCOL_HEADER_SIZE];
    uint8_t crc_bytes[UPDATE_PROTOCOL_CRC_SIZE];
    update_protocol_frame_t frame;
    uint8_t error_info_valid;
    uint8_t error_command;
    uint16_t error_sequence;
} update_protocol_parser_t;

typedef struct {
    update_protocol_parser_t parser;
    update_protocol_writer_t writer;
    const boot_flash_t *flash;
    const uint8_t *public_key;
    update_install_options_t install_options;
    update_install_result_t install_result;
    update_installer_session_t installer;
    update_protocol_session_state_t state;
    update_protocol_status_t last_status;
    uint16_t expected_sequence;
    size_t update_payload_offset;
    uint8_t installer_started;
} update_protocol_session_t;

void update_protocol_parser_init(update_protocol_parser_t *parser);
update_protocol_parse_status_t update_protocol_parser_push(
    update_protocol_parser_t *parser,
    uint8_t byte
);
const update_protocol_frame_t *update_protocol_parser_frame(
    const update_protocol_parser_t *parser
);
uint8_t update_protocol_parser_has_partial(
    const update_protocol_parser_t *parser
);
uint8_t update_protocol_parser_error_info(
    const update_protocol_parser_t *parser,
    uint8_t *command,
    uint16_t *sequence
);

uint32_t update_protocol_crc32_update(
    uint32_t crc,
    const uint8_t *data,
    size_t length
);
uint32_t update_protocol_crc32(const uint8_t *data, size_t length);

update_protocol_status_t update_protocol_encode_frame(
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *frame_size
);

update_protocol_status_t update_protocol_write_frame(
    const update_protocol_writer_t *writer,
    uint8_t command,
    uint16_t sequence,
    const uint8_t *payload,
    uint16_t payload_length
);

void update_protocol_session_init(
    update_protocol_session_t *session,
    const boot_flash_t *flash,
    const uint8_t public_key[FIRMWARE_PUBLIC_KEY_SIZE],
    const update_install_options_t *install_options,
    update_protocol_write_fn_t write,
    void *write_context
);
update_protocol_status_t update_protocol_session_accept_initial_hello(
    update_protocol_session_t *session
);
update_protocol_status_t update_protocol_session_process_byte(
    update_protocol_session_t *session,
    uint8_t byte
);
update_protocol_status_t update_protocol_session_process_reader(
    update_protocol_session_t *session,
    const byte_reader_t *reader,
    uint32_t byte_timeout_polls
);
const char *update_protocol_status_text(update_protocol_status_t status);
const char *update_protocol_session_state_text(update_protocol_session_state_t state);

#endif
