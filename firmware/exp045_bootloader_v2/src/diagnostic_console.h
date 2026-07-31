#ifndef DIAGNOSTIC_CONSOLE_H
#define DIAGNOSTIC_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

#include "boot_flash.h"
#include "byte_reader.h"
#include "reset_cause.h"

#define DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH 64U
#define DIAGNOSTIC_CONSOLE_MAX_ARGUMENTS 4U
#define DIAGNOSTIC_CONSOLE_DEFAULT_IDLE_TIMEOUT_POLLS 200000UL

typedef enum {
    DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE = 0,
    DIAGNOSTIC_CONSOLE_RESULT_RESET_REQUESTED = 1,
    DIAGNOSTIC_CONSOLE_RESULT_TIMEOUT = 2,
    DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR = 3,
    DIAGNOSTIC_CONSOLE_RESULT_RECOVERY_REQUESTED = 4
} diagnostic_console_result_t;

typedef uint8_t (*diagnostic_console_write_fn_t)(
    void *context,
    const uint8_t *data,
    size_t length
);

typedef void (*diagnostic_console_reset_fn_t)(void *context);

typedef reset_cause_t (*diagnostic_console_reset_cause_fn_t)(void *context);

typedef struct {
    const byte_reader_t *reader;
    diagnostic_console_write_fn_t write;
    void *write_context;
    const boot_flash_t *flash;
    const char *board_name;
    diagnostic_console_reset_cause_fn_t reset_cause;
    void *reset_cause_context;
    diagnostic_console_reset_fn_t reset;
    void *reset_context;
    uint32_t idle_timeout_polls;
} diagnostic_console_config_t;

typedef struct {
    char line[DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH + 1U];
    size_t line_length;
    uint32_t command_count;
    uint8_t overflowed;
    uint8_t boot_requested;
} diagnostic_console_t;

void diagnostic_console_init(diagnostic_console_t *console);
diagnostic_console_result_t diagnostic_console_run(
    diagnostic_console_t *console,
    const diagnostic_console_config_t *config,
    const char *initial_line
);
const char *diagnostic_console_result_text(
    diagnostic_console_result_t result
);

#endif
