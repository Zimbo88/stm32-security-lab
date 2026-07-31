#include "update_service.h"

#include <string.h>

#include "boot_watchdog.h"

typedef enum {
    UPDATE_SERVICE_ENTRY_NO_HELLO = 0,
    UPDATE_SERVICE_ENTRY_HELLO = 1
} update_service_entry_status_t;

static uint32_t config_or_default(uint32_t value, uint32_t default_value)
{
    return (value != 0UL) ? value : default_value;
}

static uint8_t config_is_valid(const update_service_config_t *config)
{
    if ((config == NULL) ||
        (config->flash == NULL) ||
        (config->public_key == NULL) ||
        (config->reader == NULL) ||
        (config->write == NULL) ||
        (config->install_options.program_buffer == NULL) ||
        (config->install_options.readback_buffer == NULL)) {
        return 0U;
    }

    return 1U;
}

void update_service_init(update_service_t *service)
{
    if (service != NULL) {
        memset(service, 0, sizeof(*service));
        update_protocol_parser_init(&service->entry_parser);
    }
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

static update_service_entry_status_t wait_for_binary_hello(
    update_service_t *service,
    const update_service_config_t *config
)
{
    const uint32_t max_idle_polls = config_or_default(
        config->entry_idle_polls,
        UPDATE_SERVICE_DEFAULT_ENTRY_IDLE_POLLS
    );
    const uint32_t max_bytes = config_or_default(
        config->entry_max_bytes,
        UPDATE_SERVICE_DEFAULT_ENTRY_MAX_BYTES
    );
    uint32_t idle_polls = 0U;
    uint32_t byte_count = 0U;

    update_protocol_parser_init(&service->entry_parser);

    while ((idle_polls < max_idle_polls) && (byte_count < max_bytes)) {
        boot_watchdog_refresh();
        uint8_t byte = 0U;
        const byte_reader_status_t read_status =
            byte_reader_getc_nonblocking(config->reader, &byte);

        if (read_status == BYTE_READER_NO_DATA) {
            ++idle_polls;
            continue;
        }
        if (read_status != BYTE_READER_OK) {
            return UPDATE_SERVICE_ENTRY_NO_HELLO;
        }

        idle_polls = 0U;
        ++byte_count;

        const update_protocol_parse_status_t parse_status =
            update_protocol_parser_push(&service->entry_parser, byte);

        if (parse_status == UPDATE_PROTOCOL_PARSE_NEED_MORE) {
            continue;
        }
        if (parse_status == UPDATE_PROTOCOL_PARSE_FRAME_READY) {
            const update_protocol_frame_t *frame =
                update_protocol_parser_frame(&service->entry_parser);

            if (frame_is_initial_hello(frame) != 0U) {
                return UPDATE_SERVICE_ENTRY_HELLO;
            }
            update_protocol_parser_init(&service->entry_parser);
            continue;
        }

        update_protocol_parser_init(&service->entry_parser);
    }

    return UPDATE_SERVICE_ENTRY_NO_HELLO;
}

static update_protocol_status_t accept_initial_hello(
    update_service_t *service,
    const update_service_config_t *config
)
{
    update_protocol_session_init(
        &service->protocol,
        config->flash,
        config->public_key,
        &config->install_options,
        config->write,
        config->write_context
    );

    return update_protocol_session_accept_initial_hello(&service->protocol);
}

static void abort_started_installer(update_protocol_session_t *protocol)
{
    if ((protocol != NULL) && (protocol->installer_started != 0U) &&
        (protocol->state != UPDATE_PROTOCOL_SESSION_FINISHED) &&
        (protocol->state != UPDATE_PROTOCOL_SESSION_ABORTED)) {
        (void)update_installer_abort(&protocol->installer);
        protocol->state = UPDATE_PROTOCOL_SESSION_ABORTED;
    }
}

static update_service_result_t request_reset(
    const update_service_config_t *config
)
{
    if (config->reset != NULL) {
        config->reset(config->reset_context);
    }

    return UPDATE_SERVICE_RESULT_RESET_REQUESTED;
}

static update_service_result_t run_update_protocol(
    update_service_t *service,
    const update_service_config_t *config
)
{
    const uint32_t byte_timeout = config_or_default(
        config->frame_byte_timeout_polls,
        UPDATE_SERVICE_DEFAULT_FRAME_BYTE_TIMEOUT_POLLS
    );
    const uint32_t max_frame_timeouts = config_or_default(
        config->max_frame_timeouts,
        UPDATE_SERVICE_DEFAULT_MAX_FRAME_TIMEOUTS
    );
    uint32_t frame_timeouts = 0U;

    for (;;) {
        boot_watchdog_refresh();
        const update_protocol_status_t protocol_status =
            update_protocol_session_process_reader(
                &service->protocol,
                config->reader,
                byte_timeout
            );

        if (protocol_status == UPDATE_PROTOCOL_STATUS_TIMEOUT) {
            ++frame_timeouts;
            if (frame_timeouts >= max_frame_timeouts) {
                abort_started_installer(&service->protocol);
                return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
            }
            continue;
        }
        if (protocol_status != UPDATE_PROTOCOL_STATUS_OK) {
            if ((service->protocol.state == UPDATE_PROTOCOL_SESSION_FINISHED) ||
                (service->protocol.state ==
                    UPDATE_PROTOCOL_SESSION_RESET_REQUESTED)) {
                return request_reset(config);
            }
            abort_started_installer(&service->protocol);
            return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
        }

        frame_timeouts = 0U;

        switch (service->protocol.state) {
        case UPDATE_PROTOCOL_SESSION_FINISHED:
            return request_reset(config);
        case UPDATE_PROTOCOL_SESSION_RESET_REQUESTED:
            return request_reset(config);
        case UPDATE_PROTOCOL_SESSION_ABORTED:
            return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
        case UPDATE_PROTOCOL_SESSION_FAILED:
            abort_started_installer(&service->protocol);
            return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
        case UPDATE_PROTOCOL_SESSION_IDLE:
        case UPDATE_PROTOCOL_SESSION_UPDATING:
        default:
            break;
        }
    }
}

update_service_result_t update_service_run(
    update_service_t *service,
    const update_service_config_t *config
)
{
    if ((service == NULL) || (config_is_valid(config) == 0U)) {
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
    }

    update_service_init(service);
    if (wait_for_binary_hello(service, config) != UPDATE_SERVICE_ENTRY_HELLO) {
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
    }

    if (accept_initial_hello(service, config) != UPDATE_PROTOCOL_STATUS_OK) {
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
    }

    return run_update_protocol(service, config);
}

update_service_result_t update_service_run_after_hello(
    update_service_t *service,
    const update_service_config_t *config
)
{
    if ((service == NULL) || (config_is_valid(config) == 0U)) {
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
    }

    update_service_init(service);
    if (accept_initial_hello(service, config) != UPDATE_PROTOCOL_STATUS_OK) {
        return UPDATE_SERVICE_RESULT_BOOT_CONTINUE;
    }

    return run_update_protocol(service, config);
}
