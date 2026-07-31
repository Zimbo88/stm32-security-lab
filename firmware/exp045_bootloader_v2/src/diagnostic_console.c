#include "diagnostic_console.h"

#include <string.h>

#include "board.h"
#include "board_clock.h"
#include "boot_metadata.h"
#include "boot_slot.h"
#include "image_policy.h"
#include "performance.h"
#include "stm32f429_memory_layout.h"
#include "update_protocol.h"

#define CONSOLE_PROMPT "> "

typedef enum {
    LINE_STATUS_READY = 0,
    LINE_STATUS_TIMEOUT = 1,
    LINE_STATUS_IO_ERROR = 2
} line_status_t;

static uint32_t config_timeout(const diagnostic_console_config_t *config)
{
    if ((config != NULL) && (config->idle_timeout_polls != 0UL)) {
        return config->idle_timeout_polls;
    }

    return DIAGNOSTIC_CONSOLE_DEFAULT_IDLE_TIMEOUT_POLLS;
}

static uint8_t config_is_valid(const diagnostic_console_config_t *config)
{
    if ((config == NULL) ||
        (config->reader == NULL) ||
        (config->write == NULL)) {
        return 0U;
    }

    return 1U;
}

static uint8_t console_write(
    const diagnostic_console_config_t *config,
    const uint8_t *data,
    size_t length
)
{
    if ((config == NULL) ||
        (config->write == NULL) ||
        ((length != 0U) && (data == NULL))) {
        return 0U;
    }

    return config->write(config->write_context, data, length);
}

static uint8_t console_puts(
    const diagnostic_console_config_t *config,
    const char *text
)
{
    const char *cursor = text;
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }

    while (cursor[length] != '\0') {
        ++length;
    }

    return console_write(config, (const uint8_t *)text, length);
}

static uint8_t console_put_u32(
    const diagnostic_console_config_t *config,
    uint32_t value
)
{
    char buffer[10U];
    size_t count = 0U;

    if (value == 0UL) {
        return console_puts(config, "0");
    }

    while ((value != 0UL) && (count < sizeof(buffer))) {
        buffer[count] = (char)('0' + (value % 10UL));
        value /= 10UL;
        ++count;
    }

    while (count != 0U) {
        --count;
        if (console_write(config, (const uint8_t *)&buffer[count], 1U) == 0U) {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t console_put_hex32(
    const diagnostic_console_config_t *config,
    uint32_t value
)
{
    static const char digits[] = "0123456789ABCDEF";
    char out[10U];

    out[0] = '0';
    out[1] = 'x';
    for (uint32_t i = 0U; i < 8U; ++i) {
        const uint32_t shift = 28U - (i * 4U);
        out[2U + i] = digits[(value >> shift) & 0x0FUL];
    }

    return console_write(config, (const uint8_t *)out, sizeof(out));
}

static uint8_t console_put_bool(
    const diagnostic_console_config_t *config,
    uint8_t value
)
{
    return console_puts(config, (value != 0U) ? "yes" : "no");
}

static uint8_t console_put_slot(
    const diagnostic_console_config_t *config,
    uint32_t slot
)
{
    if (slot == (uint32_t)BOOT_SLOT_A) {
        return console_puts(config, "A");
    }
    if (slot == (uint32_t)BOOT_SLOT_B) {
        return console_puts(config, "B");
    }
    if (slot == BOOT_SLOT_NONE) {
        return console_puts(config, "none");
    }

    return console_put_hex32(config, slot);
}

static uint8_t str_equal(const char *left, const char *right)
{
    size_t index = 0U;

    if ((left == NULL) || (right == NULL)) {
        return 0U;
    }

    while ((left[index] != '\0') && (right[index] != '\0')) {
        if (left[index] != right[index]) {
            return 0U;
        }
        ++index;
    }

    return (left[index] == right[index]) ? 1U : 0U;
}

static uint8_t is_space(char c)
{
    return ((c == ' ') || (c == '\t')) ? 1U : 0U;
}

static uint8_t tokenize(
    char *line,
    char *argv[DIAGNOSTIC_CONSOLE_MAX_ARGUMENTS],
    size_t *argc,
    uint8_t *too_many
)
{
    char *cursor = line;
    size_t count = 0U;

    if ((line == NULL) || (argv == NULL) ||
        (argc == NULL) || (too_many == NULL)) {
        return 0U;
    }

    *argc = 0U;
    *too_many = 0U;

    while (*cursor != '\0') {
        while (is_space(*cursor) != 0U) {
            *cursor = '\0';
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        if (count >= DIAGNOSTIC_CONSOLE_MAX_ARGUMENTS) {
            *too_many = 1U;
            *argc = count;
            return 1U;
        }

        argv[count] = cursor;
        ++count;

        while ((*cursor != '\0') && (is_space(*cursor) == 0U)) {
            ++cursor;
        }
    }

    *argc = count;
    return 1U;
}

static uint8_t print_slot_descriptor(
    const diagnostic_console_config_t *config,
    uint32_t slot_id
)
{
    const boot_slot_descriptor_t *slot = NULL;

    if (boot_slot_lookup(slot_id, &slot) != BOOT_SLOT_LOOKUP_OK) {
        return console_puts(config, "slot lookup failed\n");
    }

    if (console_puts(config, "slot ") == 0U ||
        console_put_slot(config, slot_id) == 0U ||
        console_puts(config, ": signed=") == 0U ||
        console_put_hex32(config, slot->signed_image_base) == 0U ||
        console_puts(config, " payload=") == 0U ||
        console_put_hex32(config, slot->payload_base) == 0U ||
        console_puts(config, " end=") == 0U ||
        console_put_hex32(config, slot->slot_end) == 0U ||
        console_puts(config, " max=") == 0U ||
        console_put_u32(config, slot->maximum_payload_size) == 0U ||
        console_puts(config, " sectors=") == 0U ||
        console_put_u32(config, slot->first_sector) == 0U ||
        console_puts(config, "..") == 0U ||
        console_put_u32(config, slot->last_sector) == 0U ||
        console_puts(config, "\n") == 0U) {
        return 0U;
    }

    return 1U;
}

static uint8_t print_reset_flag(
    const diagnostic_console_config_t *config,
    const char *name,
    uint8_t value
)
{
    return console_puts(config, name) &&
        console_puts(config, "=") &&
        console_put_bool(config, value) &&
        console_puts(config, "\n");
}

static uint8_t command_help(const diagnostic_console_config_t *config)
{
    return console_puts(
        config,
        "commands: help version info slots metadata flashinfo "
        "resetcause performance status boot reboot recovery\n"
    );
}

static uint8_t command_version(const diagnostic_console_config_t *config)
{
    const char *name = (config->board_name != NULL)
        ? config->board_name
        : BOARD_NAME;

    return console_puts(config, "bootloader=EXP045 V2\n") &&
        console_puts(config, "board=") &&
        console_puts(config, name) &&
        console_puts(config, "\nlayout=") &&
        console_puts(config, STM32F429_LAYOUT_PROFILE_NAME) &&
        console_puts(config, "\nprotocol=") &&
        console_put_u32(config, UPDATE_PROTOCOL_VERSION) &&
        console_puts(config, "\npackage=") &&
        console_put_u32(config, UPDATE_PACKAGE_FORMAT_VERSION) &&
        console_puts(config, "\nmin-image-version=") &&
        console_put_u32(config, MIN_IMAGE_VERSION) &&
        console_puts(config, "\n");
}

static uint8_t command_info(const diagnostic_console_config_t *config)
{
    return console_puts(config, "cpu-hz=") &&
        console_put_u32(config, board_clock_get_sysclk_hz()) &&
        console_puts(config, "\nflash-base=") &&
        console_put_hex32(config, STM32F429_FLASH_BASE) &&
        console_puts(config, "\nflash-end=") &&
        console_put_hex32(config, STM32F429_FLASH_END) &&
        console_puts(config, "\nbootloader-end=") &&
        console_put_hex32(config, STM32F429_BOOTLOADER_END) &&
        console_puts(config, "\napp-base=") &&
        console_put_hex32(config, STM32F429_APPLICATION_BASE) &&
        console_puts(config, "\n");
}

static uint8_t command_slots(const diagnostic_console_config_t *config)
{
    return print_slot_descriptor(config, (uint32_t)BOOT_SLOT_A) &&
        print_slot_descriptor(config, (uint32_t)BOOT_SLOT_B);
}

static uint8_t command_metadata(const diagnostic_console_config_t *config)
{
    boot_metadata_record_t record;
    boot_metadata_recovery_t recovery;
    boot_metadata_status_t status;

    if (config->flash == NULL) {
        return console_puts(config, "metadata-status=flash-unavailable\n");
    }

    (void)boot_metadata_empty(&record);
    recovery.copy_a_valid = 0U;
    recovery.copy_b_valid = 0U;
    recovery.selected_copy = BOOT_METADATA_COPY_NONE;

    status = boot_metadata_recover_from_flash(
        config->flash,
        &record,
        &recovery
    );

    if (console_puts(config, "metadata-status=") == 0U ||
        console_puts(config, boot_metadata_status_text(status)) == 0U ||
        console_puts(config, "\ncopy-a-valid=") == 0U ||
        console_put_bool(config, recovery.copy_a_valid) == 0U ||
        console_puts(config, "\ncopy-b-valid=") == 0U ||
        console_put_bool(config, recovery.copy_b_valid) == 0U ||
        console_puts(config, "\nselected-copy=") == 0U) {
        return 0U;
    }

    if (recovery.selected_copy == BOOT_METADATA_COPY_A) {
        if (console_puts(config, "A") == 0U) {
            return 0U;
        }
    } else if (recovery.selected_copy == BOOT_METADATA_COPY_B) {
        if (console_puts(config, "B") == 0U) {
            return 0U;
        }
    } else if (console_puts(config, "none") == 0U) {
        return 0U;
    }

    return console_puts(config, "\nsequence=") &&
        console_put_u32(config, record.sequence) &&
        console_puts(config, "\nstate=") &&
        console_puts(config, boot_metadata_state_text(record.state)) &&
        console_puts(config, "\nactive-slot=") &&
        console_put_slot(config, record.active_slot) &&
        console_puts(config, "\ncandidate-slot=") &&
        console_put_slot(config, record.candidate_slot) &&
        console_puts(config, "\ncandidate-version=") &&
        console_put_u32(config, record.candidate_image_version) &&
        console_puts(config, "\nattempts=") &&
        console_put_u32(config, record.boot_attempt_count) &&
        console_puts(config, "\nconfirmed=") &&
        console_put_bool(config, (uint8_t)record.confirmation_state) &&
        console_puts(config, "\nresult=") &&
        console_put_hex32(config, record.result) &&
        console_puts(config, "\n");
}

static uint8_t command_flashinfo(const diagnostic_console_config_t *config)
{
    return console_puts(config, "flash-size=") &&
        console_put_u32(config, STM32F429_FLASH_TOTAL_SIZE) &&
        console_puts(config, "\nsector-count=") &&
        console_put_u32(config, STM32F429_FLASH_SECTOR_COUNT) &&
        console_puts(config, "\nmetadata-a=") &&
        console_put_hex32(config, STM32F429_BOOT_METADATA_A_BASE) &&
        console_puts(config, "..") &&
        console_put_hex32(config, STM32F429_BOOT_METADATA_A_END) &&
        console_puts(config, "\nmetadata-b=") &&
        console_put_hex32(config, STM32F429_BOOT_METADATA_B_BASE) &&
        console_puts(config, "..") &&
        console_put_hex32(config, STM32F429_BOOT_METADATA_B_END) &&
        console_puts(config, "\nrecovery=") &&
        console_put_hex32(config, STM32F429_RECOVERY_BASE) &&
        console_puts(config, "..") &&
        console_put_hex32(config, STM32F429_RECOVERY_END) &&
        console_puts(config, "\n");
}

static uint8_t command_resetcause(const diagnostic_console_config_t *config)
{
    reset_cause_t cause;

    if (config->reset_cause == NULL) {
        return console_puts(config, "resetcause=unavailable\n");
    }

    cause = config->reset_cause(config->reset_cause_context);
    return console_puts(config, "raw=") &&
        console_put_hex32(config, cause.raw_csr) &&
        console_puts(config, "\n") &&
        print_reset_flag(config, "low-power", cause.low_power_reset) &&
        print_reset_flag(config, "window-watchdog", cause.window_watchdog_reset) &&
        print_reset_flag(
            config,
            "independent-watchdog",
            cause.independent_watchdog_reset
        ) &&
        print_reset_flag(config, "software", cause.software_reset) &&
        print_reset_flag(config, "power-on", cause.power_on_reset) &&
        print_reset_flag(config, "pin", cause.pin_reset) &&
        print_reset_flag(config, "brownout", cause.brownout_reset);
}

static uint8_t command_performance(const diagnostic_console_config_t *config)
{
    return console_puts(config, "cpu-hz=") &&
        console_put_u32(config, board_clock_get_sysclk_hz()) &&
        console_puts(config, "\nsha512-us=") &&
        console_put_u32(config, performance_sha512_us()) &&
        console_puts(config, "\ned25519-us=") &&
        console_put_u32(config, performance_ed25519_us()) &&
        console_puts(config, "\nverify-us=") &&
        console_put_u32(config, performance_verification_us()) &&
        console_puts(config, "\n");
}

static uint8_t command_status(
    diagnostic_console_t *console,
    const diagnostic_console_config_t *config
)
{
    return console_puts(config, "console=ready\ncommands=") &&
        console_put_u32(config, console->command_count) &&
        console_puts(config, "\nreadonly=yes\n");
}

static uint8_t copy_initial_line(
    diagnostic_console_t *console,
    const char *initial_line
)
{
    size_t index = 0U;

    if ((console == NULL) || (initial_line == NULL)) {
        return 0U;
    }

    while (initial_line[index] != '\0') {
        if (index >= DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH) {
            console->line_length = 0U;
            console->line[0] = '\0';
            console->overflowed = 1U;
            return 1U;
        }
        console->line[index] = initial_line[index];
        ++index;
    }

    console->line[index] = '\0';
    console->line_length = index;
    console->overflowed = 0U;
    return 1U;
}

static diagnostic_console_result_t execute_line(
    diagnostic_console_t *console,
    const diagnostic_console_config_t *config
)
{
    char *argv[DIAGNOSTIC_CONSOLE_MAX_ARGUMENTS];
    size_t argc = 0U;
    uint8_t too_many = 0U;
    uint8_t ok = 1U;

    if ((console == NULL) || (config == NULL)) {
        return DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    }

    if (console->overflowed != 0U) {
        console->overflowed = 0U;
        console->line_length = 0U;
        console->line[0] = '\0';
        return console_puts(config, "ERR line-too-long\n") != 0U
            ? DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE
            : DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    }

    if (tokenize(console->line, argv, &argc, &too_many) == 0U) {
        return DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    }

    if (argc == 0U) {
        console->line_length = 0U;
        return DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE;
    }

    console->line_length = 0U;
    ++console->command_count;

    if (too_many != 0U) {
        ok = console_puts(config, "ERR args\n");
    } else if (argc != 1U) {
        ok = console_puts(config, "ERR args\n");
    } else if (str_equal(argv[0], "help") != 0U) {
        ok = command_help(config);
    } else if (str_equal(argv[0], "version") != 0U) {
        ok = command_version(config);
    } else if (str_equal(argv[0], "info") != 0U) {
        ok = command_info(config);
    } else if (str_equal(argv[0], "slots") != 0U) {
        ok = command_slots(config);
    } else if (str_equal(argv[0], "metadata") != 0U) {
        ok = command_metadata(config);
    } else if (str_equal(argv[0], "flashinfo") != 0U) {
        ok = command_flashinfo(config);
    } else if (str_equal(argv[0], "resetcause") != 0U) {
        ok = command_resetcause(config);
    } else if (str_equal(argv[0], "performance") != 0U) {
        ok = command_performance(config);
    } else if (str_equal(argv[0], "status") != 0U) {
        ok = command_status(console, config);
    } else if (str_equal(argv[0], "boot") != 0U) {
        ok = console_puts(config, "boot\n");
        console->boot_requested = 1U;
        return ok != 0U
            ? DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE
            : DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    } else if (str_equal(argv[0], "reboot") != 0U) {
        ok = console_puts(config, "reboot\n");
        if (ok != 0U && config->reset != NULL) {
            config->reset(config->reset_context);
        }
        return ok != 0U
            ? DIAGNOSTIC_CONSOLE_RESULT_RESET_REQUESTED
            : DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    } else if (str_equal(argv[0], "recovery") != 0U) {
        ok = console_puts(config, "recovery\n");
        return ok != 0U
            ? DIAGNOSTIC_CONSOLE_RESULT_RECOVERY_REQUESTED
            : DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    } else {
        ok = console_puts(config, "ERR unknown\n");
    }

    return ok != 0U
        ? DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE
        : DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
}

static line_status_t read_line(
    diagnostic_console_t *console,
    const diagnostic_console_config_t *config
)
{
    for (;;) {
        uint8_t byte = 0U;
        const byte_reader_status_t status = byte_reader_getc_timeout(
            config->reader,
            &byte,
            config_timeout(config)
        );

        if (status == BYTE_READER_TIMEOUT) {
            return LINE_STATUS_TIMEOUT;
        }
        if (status != BYTE_READER_OK) {
            return LINE_STATUS_IO_ERROR;
        }

        if ((byte == '\r') || (byte == '\n')) {
            console->line[console->line_length] = '\0';
            return LINE_STATUS_READY;
        }

        if ((byte == '\b') || (byte == 0x7FU)) {
            if (console->line_length != 0U) {
                --console->line_length;
                console->line[console->line_length] = '\0';
            }
            continue;
        }

        if (byte == '\t') {
            byte = ' ';
        } else if ((byte < 0x20U) || (byte > 0x7EU)) {
            console->line_length = 0U;
            console->line[0] = '\0';
            console->overflowed = 0U;
            if (console_puts(config, "ERR control\n") == 0U) {
                return LINE_STATUS_IO_ERROR;
            }
            return LINE_STATUS_READY;
        }

        if (console->line_length >= DIAGNOSTIC_CONSOLE_MAX_LINE_LENGTH) {
            console->overflowed = 1U;
            continue;
        }

        console->line[console->line_length] = (char)byte;
        ++console->line_length;
        console->line[console->line_length] = '\0';
    }
}

void diagnostic_console_init(diagnostic_console_t *console)
{
    if (console != NULL) {
        memset(console, 0, sizeof(*console));
    }
}

diagnostic_console_result_t diagnostic_console_run(
    diagnostic_console_t *console,
    const diagnostic_console_config_t *config,
    const char *initial_line
)
{
    if ((console == NULL) || (config_is_valid(config) == 0U)) {
        return DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    }

    diagnostic_console_init(console);

    if (console_puts(config, "diagnostic console readonly\n") == 0U) {
        return DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
    }

    if (initial_line != NULL) {
        diagnostic_console_result_t result;

        if (copy_initial_line(console, initial_line) == 0U) {
            return DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
        }

        result = execute_line(console, config);
        if ((result == DIAGNOSTIC_CONSOLE_RESULT_RESET_REQUESTED) ||
            (result == DIAGNOSTIC_CONSOLE_RESULT_RECOVERY_REQUESTED) ||
            (console->boot_requested != 0U) ||
            (result == DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR)) {
            return result;
        }
    }

    for (;;) {
        const line_status_t line_status =
            console_puts(config, CONSOLE_PROMPT) != 0U
                ? read_line(console, config)
                : LINE_STATUS_IO_ERROR;
        diagnostic_console_result_t result;

        if (line_status == LINE_STATUS_TIMEOUT) {
            (void)console_puts(config, "timeout\n");
            return DIAGNOSTIC_CONSOLE_RESULT_TIMEOUT;
        }
        if (line_status != LINE_STATUS_READY) {
            return DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR;
        }

        result = execute_line(console, config);
        if ((result == DIAGNOSTIC_CONSOLE_RESULT_RESET_REQUESTED) ||
            (result == DIAGNOSTIC_CONSOLE_RESULT_RECOVERY_REQUESTED) ||
            (result == DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR)) {
            return result;
        }

        if (console->boot_requested != 0U) {
            return DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE;
        }
    }
}

const char *diagnostic_console_result_text(
    diagnostic_console_result_t result
)
{
    switch (result) {
    case DIAGNOSTIC_CONSOLE_RESULT_BOOT_CONTINUE:   return "BOOT CONTINUE";
    case DIAGNOSTIC_CONSOLE_RESULT_RESET_REQUESTED: return "RESET REQUESTED";
    case DIAGNOSTIC_CONSOLE_RESULT_TIMEOUT:         return "TIMEOUT";
    case DIAGNOSTIC_CONSOLE_RESULT_IO_ERROR:        return "IO ERROR";
    case DIAGNOSTIC_CONSOLE_RESULT_RECOVERY_REQUESTED:
        return "RECOVERY REQUESTED";
    default:                                        return "UNKNOWN";
    }
}
