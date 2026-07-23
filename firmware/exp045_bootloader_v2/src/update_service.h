#ifndef UPDATE_SERVICE_H
#define UPDATE_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "byte_reader.h"
#include "update_installer.h"
#include "update_protocol.h"

#define UPDATE_SERVICE_DEFAULT_ENTRY_IDLE_POLLS          200000UL
#define UPDATE_SERVICE_DEFAULT_ENTRY_MAX_BYTES \
    (UPDATE_PROTOCOL_MAX_FRAME_SIZE * 2U)
#define UPDATE_SERVICE_DEFAULT_FRAME_BYTE_TIMEOUT_POLLS  200000UL
#define UPDATE_SERVICE_DEFAULT_MAX_FRAME_TIMEOUTS        3UL

typedef enum {
    UPDATE_SERVICE_RESULT_BOOT_CONTINUE = 0,
    UPDATE_SERVICE_RESULT_RESET_REQUESTED = 1
} update_service_result_t;

typedef void (*update_service_reset_fn_t)(void *context);

typedef struct {
    update_protocol_session_t protocol;
    update_protocol_parser_t entry_parser;
} update_service_t;

typedef struct {
    const boot_flash_t *flash;
    const uint8_t *public_key;
    update_install_options_t install_options;
    const byte_reader_t *reader;
    update_protocol_write_fn_t write;
    void *write_context;
    update_service_reset_fn_t reset;
    void *reset_context;
    uint32_t entry_idle_polls;
    uint32_t entry_max_bytes;
    uint32_t frame_byte_timeout_polls;
    uint32_t max_frame_timeouts;
} update_service_config_t;

void update_service_init(update_service_t *service);
update_service_result_t update_service_run(
    update_service_t *service,
    const update_service_config_t *config
);
update_service_result_t update_service_run_after_hello(
    update_service_t *service,
    const update_service_config_t *config
);

#endif
