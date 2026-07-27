#include "update_mode.h"

#include "board_version.h"
#include "boot_flash_target.h"
#include "diagnostic_console.h"
#include "firmware_public_key.h"
#include "reset_cause.h"
#include "system_reset.h"
#include "uart.h"

#define UPDATE_MODE_PROGRAM_BUFFER_SIZE 512U

typedef enum {
    UPDATE_MODE_ENTRY_BOOT = 0,
    UPDATE_MODE_ENTRY_BINARY_HELLO = 1,
    UPDATE_MODE_ENTRY_TEXT_CONSOLE = 2
} update_mode_entry_t;

static update_service_t update_mode_service;
static diagnostic_console_t update_mode_console;
static boot_flash_t update_mode_flash;
static uint8_t update_mode_program_buffer[UPDATE_MODE_PROGRAM_BUFFER_SIZE];
static uint8_t update_mode_readback_buffer[UPDATE_MODE_PROGRAM_BUFFER_SIZE];

static update_protocol_status_t uart_binary_write(
    void *context,
    const uint8_t *data,
    size_t length
)
{
    (void)context;

    if ((length != 0U) && (data == NULL)) {
        return UPDATE_PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    for (size_t i = 0U; i < length; ++i) {
        uart_putc((char)data[i]);
    }

    return (uart_wait_tx_complete() == UART_OK)
        ? UPDATE_PROTOCOL_STATUS_OK
        : UPDATE_PROTOCOL_STATUS_IO_ERROR;
}

static uint8_t uart_console_write(
    void *context,
    const uint8_t *data,
    size_t length
)
{
    (void)context;

    if ((length != 0U) && (data == NULL)) {
        return 0U;
    }

    for (size_t i = 0U; i < length; ++i) {
        uart_putc((char)data[i]);
    }

    return 1U;
}

static void update_mode_reset(void *context)
{
    (void)context;
    system_reset_request();
}

static reset_cause_t update_mode_reset_cause(void *context)
{
    (void)context;
    return reset_cause_capture();
}

static uint8_t frame_is_initial_hello(const update_protocol_frame_t *frame)
{
    if (frame == NULL) {
        return 0U;
    }

    return ((frame->version == UPDATE_PROTOCOL_VERSION) &&
            (frame->command == (uint8_t)UPDATE_PROTOCOL_CMD_HELLO) &&
            (frame->sequence == 0U) &&
            (frame->payload_length == 0U))
        ? 1U
        : 0U;
}

static void reset_text_line(
    char *line,
    size_t *line_length,
    uint8_t *line_overflow
)
{
    if (line != NULL) {
        line[0] = '\0';
    }
    if (line_length != NULL) {
        *line_length = 0U;
    }
    if (line_overflow != NULL) {
        *line_overflow = 0U;
    }
}

static void entry_text_accept_byte(
    uint8_t byte,
    char *line,
    size_t *line_length,
    uint8_t *line_overflow
)
{
    if ((line == NULL) || (line_length == NULL) ||
        (line_overflow == NULL)) {
        return;
    }

    if ((byte == '\b') || (byte == 0x7FU)) {
        if (*line_length != 0U) {
            --(*line_length);
            line[*line_length] = '\0';
        }
        return;
    }

    if (byte == '\t') {
        byte = ' ';
    } else if ((byte < 0x20U) || (byte > 0x7EU)) {
        reset_text_line(line, line_length, line_overflow);
        return;
    }

    if (*line_length >= DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH) {
        *line_overflow = 1U;
        return;
    }

    line[*line_length] = (char)byte;
    ++(*line_length);
    line[*line_length] = '\0';
}

static update_mode_entry_t wait_for_update_or_console_entry(
    const byte_reader_t *reader,
    char *text_line,
    size_t text_line_capacity
)
{
    uint32_t idle_polls = 0U;
    uint32_t byte_count = 0U;
    size_t line_length = 0U;
    uint8_t line_overflow = 0U;

    if ((reader == NULL) ||
        (text_line == NULL) ||
        (text_line_capacity <= DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH)) {
        return UPDATE_MODE_ENTRY_BOOT;
    }

    text_line[0] = '\0';
    update_protocol_parser_init(&update_mode_service.entry_parser);

    while ((idle_polls < UPDATE_SERVICE_DEFAULT_ENTRY_IDLE_POLLS) &&
           (byte_count < UPDATE_SERVICE_DEFAULT_ENTRY_MAX_BYTES)) {
        uint8_t byte = 0U;
        const byte_reader_status_t read_status =
            byte_reader_getc_nonblocking(reader, &byte);

        if (read_status == BYTE_READER_NO_DATA) {
            ++idle_polls;
            continue;
        }
        if (read_status != BYTE_READER_OK) {
            return UPDATE_MODE_ENTRY_BOOT;
        }

        idle_polls = 0U;
        ++byte_count;

        const update_protocol_parse_status_t parse_status =
            update_protocol_parser_push(&update_mode_service.entry_parser, byte);

        if (parse_status == UPDATE_PROTOCOL_PARSE_FRAME_READY) {
            if (frame_is_initial_hello(
                    update_protocol_parser_frame(&update_mode_service.entry_parser)
                ) != 0U) {
                return UPDATE_MODE_ENTRY_BINARY_HELLO;
            }
            update_protocol_parser_init(&update_mode_service.entry_parser);
        } else if (parse_status != UPDATE_PROTOCOL_PARSE_NEED_MORE) {
            update_protocol_parser_init(&update_mode_service.entry_parser);
        }

        if ((byte == '\r') || (byte == '\n')) {
            text_line[line_length] = '\0';
            if (line_overflow != 0U) {
                text_line[0] = '\0';
            }
            return UPDATE_MODE_ENTRY_TEXT_CONSOLE;
        }

        entry_text_accept_byte(
            byte,
            text_line,
            &line_length,
            &line_overflow
        );
    }

    return UPDATE_MODE_ENTRY_BOOT;
}

update_service_result_t update_mode_poll_and_process(void)
{
    byte_reader_t reader;
    update_service_config_t config;
    diagnostic_console_config_t console_config;
    char initial_line[DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH + 1U];

    if (boot_flash_target_init(&update_mode_flash) != BOOT_FLASH_OK) {
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
    }

    uart_byte_reader_init(&reader);

    config.flash = &update_mode_flash;
    config.public_key = firmware_public_key;
    config.install_options.program_buffer = update_mode_program_buffer;
    config.install_options.program_buffer_size =
        sizeof(update_mode_program_buffer);
    config.install_options.readback_buffer = update_mode_readback_buffer;
    config.install_options.readback_buffer_size =
        sizeof(update_mode_readback_buffer);
    config.install_options.fault_hook = NULL;
    config.install_options.fault_context = NULL;
    config.reader = &reader;
    config.write = uart_binary_write;
    config.write_context = NULL;
    config.reset = update_mode_reset;
    config.reset_context = NULL;
    config.entry_idle_polls = UPDATE_SERVICE_DEFAULT_ENTRY_IDLE_POLLS;
    config.entry_max_bytes = UPDATE_SERVICE_DEFAULT_ENTRY_MAX_BYTES;
    config.frame_byte_timeout_polls =
        UPDATE_SERVICE_DEFAULT_FRAME_BYTE_TIMEOUT_POLLS;
    config.max_frame_timeouts =
        UPDATE_SERVICE_DEFAULT_MAX_FRAME_TIMEOUTS;

    switch (wait_for_update_or_console_entry(
        &reader,
        initial_line,
        sizeof(initial_line)
    )) {
    case UPDATE_MODE_ENTRY_BINARY_HELLO:
        return update_service_run_after_hello(&update_mode_service, &config);

    case UPDATE_MODE_ENTRY_TEXT_CONSOLE:
        console_config.reader = &reader;
        console_config.write = uart_console_write;
        console_config.write_context = NULL;
        console_config.flash = &update_mode_flash;
        console_config.board_name = board_name();
        console_config.reset_cause = update_mode_reset_cause;
        console_config.reset_cause_context = NULL;
        console_config.reset = update_mode_reset;
        console_config.reset_context = NULL;
        console_config.idle_timeout_polls =
            DIAGNOSTIC_CONSOLE_DEFAULT_IDLE_TIMEOUT_POLLS;

        if (diagnostic_console_run(
                &update_mode_console,
                &console_config,
                initial_line
            ) == DIAGNOSTIC_CONSOLE_RESULT_RESET_REQUESTED) {
            return UPDATE_SERVICE_RESULT_RESET_REQUESTED;
        }
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;

    case UPDATE_MODE_ENTRY_BOOT:
    default:
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
    }
}
