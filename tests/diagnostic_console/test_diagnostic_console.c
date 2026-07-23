#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_flash.h"
#include "diagnostic_console.h"
#include "simulated_flash.h"

#define CAPTURE_CAPACITY 8192U

static int failures;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} array_reader_t;

typedef struct {
    uint8_t data[CAPTURE_CAPACITY];
    size_t length;
} capture_writer_t;

typedef struct {
    uint32_t count;
} reset_capture_t;

typedef struct {
    simulated_flash_t sim;
    boot_flash_t flash;
    byte_reader_t reader;
    array_reader_t input;
    capture_writer_t output;
    reset_capture_t reset;
    reset_cause_t reset_cause;
    diagnostic_console_config_t config;
} test_context_t;

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

static void expect_result(
    const char *name,
    diagnostic_console_result_t expected,
    diagnostic_console_result_t actual
)
{
    if (expected != actual) {
        printf(
            "%s: expected %s, got %s\n",
            name,
            diagnostic_console_result_text(expected),
            diagnostic_console_result_text(actual)
        );
        ++failures;
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

    *byte = reader->data[reader->offset];
    reader->offset += 1U;
    return BYTE_READER_OK;
}

static uint8_t capture_write(
    void *context,
    const uint8_t *data,
    size_t length
)
{
    capture_writer_t *capture = (capture_writer_t *)context;

    if ((capture == NULL) ||
        ((length != 0U) && (data == NULL)) ||
        (length > (sizeof(capture->data) - capture->length))) {
        return 0U;
    }

    if (length != 0U) {
        memcpy(&capture->data[capture->length], data, length);
        capture->length += length;
    }
    return 1U;
}

static void capture_reset(void *context)
{
    reset_capture_t *reset = (reset_capture_t *)context;

    if (reset != NULL) {
        reset->count += 1UL;
    }
}

static reset_cause_t capture_reset_cause(void *context)
{
    const reset_cause_t *cause = (const reset_cause_t *)context;
    reset_cause_t empty;

    if (cause != NULL) {
        return *cause;
    }

    memset(&empty, 0, sizeof(empty));
    return empty;
}

static void init_context(
    test_context_t *context,
    const uint8_t *input,
    size_t input_length
)
{
    memset(context, 0, sizeof(*context));
    simulated_flash_init(&context->sim);

    if (boot_flash_init(
            &context->flash,
            &context->sim,
            simulated_flash_ops(),
            NULL,
            0U,
            8U
        ) != BOOT_FLASH_OK) {
        printf("boot_flash_init failed\n");
        ++failures;
    }

    context->input.data = input;
    context->input.length = input_length;
    context->input.offset = 0U;
    byte_reader_init(&context->reader, array_getc, &context->input);

    context->reset_cause.raw_csr = 0xA5000000UL;
    context->reset_cause.software_reset = 1U;
    context->reset_cause.pin_reset = 1U;

    context->config.reader = &context->reader;
    context->config.write = capture_write;
    context->config.write_context = &context->output;
    context->config.flash = &context->flash;
    context->config.board_name = "host-test-board";
    context->config.reset_cause = capture_reset_cause;
    context->config.reset_cause_context = &context->reset_cause;
    context->config.reset = capture_reset;
    context->config.reset_context = &context->reset;
    context->config.idle_timeout_polls = 1U;
}

static diagnostic_console_result_t run_console(
    const char *initial_line,
    const uint8_t *input,
    size_t input_length,
    test_context_t *context
)
{
    diagnostic_console_t console;

    init_context(context, input, input_length);
    return diagnostic_console_run(&console, &context->config, initial_line);
}

static uint8_t output_contains(
    const test_context_t *context,
    const char *needle
)
{
    const size_t needle_length = strlen(needle);

    if ((context == NULL) || (needle == NULL) ||
        (needle_length == 0U) ||
        (needle_length > context->output.length)) {
        return 0U;
    }

    for (size_t offset = 0U;
         offset <= (context->output.length - needle_length);
         ++offset) {
        if (memcmp(&context->output.data[offset], needle, needle_length) == 0) {
            return 1U;
        }
    }

    return 0U;
}

static void expect_contains(
    const char *name,
    const test_context_t *context,
    const char *needle
)
{
    if (output_contains(context, needle) == 0U) {
        printf("%s: output does not contain '%s'\n", name, needle);
        ++failures;
    }
}

static void expect_not_contains(
    const char *name,
    const test_context_t *context,
    const char *needle
)
{
    if (output_contains(context, needle) != 0U) {
        printf("%s: output unexpectedly contains '%s'\n", name, needle);
        ++failures;
    }
}

static void expect_no_flash_writes(
    const char *name,
    const test_context_t *context
)
{
    expect_u32(name, 0U, context->sim.erase_count);
    expect_u32(name, 0U, context->sim.program_count);
}

static void test_empty_input_line(void)
{
    static const uint8_t input[] = "\nboot\n";
    test_context_t context;

    expect_result(
        "empty line boots after boot command",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console(NULL, input, sizeof(input) - 1U, &context)
    );
    expect_contains("empty line prompt", &context, "> ");
    expect_not_contains("empty line no err", &context, "ERR");
    expect_no_flash_writes("empty line flash writes", &context);
}

static void test_too_long_line(void)
{
    uint8_t input[DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH + 8U];
    test_context_t context;

    memset(input, 'a', sizeof(input));
    input[sizeof(input) - 7U] = '\n';
    input[sizeof(input) - 6U] = 'b';
    input[sizeof(input) - 5U] = 'o';
    input[sizeof(input) - 4U] = 'o';
    input[sizeof(input) - 3U] = 't';
    input[sizeof(input) - 2U] = '\n';
    input[sizeof(input) - 1U] = '\0';

    expect_result(
        "too long line",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console(NULL, input, sizeof(input) - 1U, &context)
    );
    expect_contains("too long error", &context, "ERR line-too-long\n");
    expect_no_flash_writes("too long flash writes", &context);
}

static void test_unknown_command(void)
{
    static const uint8_t input[] = "boot\n";
    test_context_t context;

    expect_result(
        "unknown command",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console("wat", input, sizeof(input) - 1U, &context)
    );
    expect_contains("unknown error", &context, "ERR unknown\n");
    expect_no_flash_writes("unknown flash writes", &context);
}

static void test_additional_arguments(void)
{
    static const uint8_t input[] = "boot\n";
    test_context_t context;

    expect_result(
        "extra args",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console("help now", input, sizeof(input) - 1U, &context)
    );
    expect_contains("extra args error", &context, "ERR args\n");
    expect_no_flash_writes("extra args flash writes", &context);
}

static void test_control_characters(void)
{
    static const uint8_t input[] = {
        's', 't', 0x01U, 'a', 't', 'u', 's', '\n',
        'b', 'o', 'o', 't', '\n'
    };
    test_context_t context;

    expect_result(
        "control chars",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console(NULL, input, sizeof(input), &context)
    );
    expect_contains("control error", &context, "ERR control\n");
    expect_no_flash_writes("control flash writes", &context);
}

static void test_repeated_commands(void)
{
    static const uint8_t input[] = "status\nstatus\nboot\n";
    test_context_t context;

    expect_result(
        "repeated commands",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console(NULL, input, sizeof(input) - 1U, &context)
    );
    expect_contains("first status", &context, "commands=1\n");
    expect_contains("second status", &context, "commands=2\n");
    expect_no_flash_writes("repeated flash writes", &context);
}

static void test_timeout(void)
{
    test_context_t context;

    expect_result(
        "timeout",
        DIAGNOSTIC_CONSOLE_RESULT_TIMEOUT,
        run_console(NULL, NULL, 0U, &context)
    );
    expect_contains("timeout output", &context, "timeout\n");
    expect_no_flash_writes("timeout flash writes", &context);
}

static void test_transition_to_boot(void)
{
    static const uint8_t input[] = "boot\n";
    test_context_t context;

    expect_result(
        "boot command",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console(NULL, input, sizeof(input) - 1U, &context)
    );
    expect_contains("boot output", &context, "boot\n");
    expect_u32("boot no reset", 0U, context.reset.count);
    expect_no_flash_writes("boot flash writes", &context);
}

static void test_reboot_requests_reset(void)
{
    static const uint8_t input[] = "reboot\n";
    test_context_t context;

    expect_result(
        "reboot command",
        DIAGNOSTIC_CONSOLE_RESULT_RESET_REQUESTED,
        run_console(NULL, input, sizeof(input) - 1U, &context)
    );
    expect_contains("reboot output", &context, "reboot\n");
    expect_u32("reboot reset count", 1U, context.reset.count);
    expect_no_flash_writes("reboot flash writes", &context);
}

static void test_readonly_information_commands(void)
{
    static const uint8_t input[] =
        "help\nversion\ninfo\nslots\nmetadata\nflashinfo\n"
        "resetcause\nperformance\nstatus\nboot\n";
    test_context_t context;

    expect_result(
        "readonly commands",
        DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE,
        run_console(NULL, input, sizeof(input) - 1U, &context)
    );
    expect_contains("help output", &context, "commands: help version");
    expect_contains("version output", &context, "board=host-test-board\n");
    expect_contains("slots output", &context, "slot A:");
    expect_contains("metadata output", &context, "metadata-status=");
    expect_contains("flashinfo output", &context, "sector-count=");
    expect_contains("resetcause output", &context, "raw=0xA5000000");
    expect_contains("performance output", &context, "verify-us=");
    expect_contains("status output", &context, "readonly=yes\n");
    expect_no_flash_writes("readonly flash writes", &context);
}

int main(void)
{
    test_empty_input_line();
    test_too_long_line();
    test_unknown_command();
    test_additional_arguments();
    test_control_characters();
    test_repeated_commands();
    test_timeout();
    test_transition_to_boot();
    test_reboot_requests_reset();
    test_readonly_information_commands();

    if (failures != 0) {
        printf("diagnostic console tests failed: %d\n", failures);
        return 1;
    }

    return 0;
}
