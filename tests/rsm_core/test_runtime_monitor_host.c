#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "experiment_telemetry.h"
#include "log.h"
#include "platform_health.h"
#include "runtime_monitor.h"

static char uart_output[1024];
static uint32_t uart_output_len;
static uint32_t last_logged_event;

static void reset_output(void)
{
    uart_output_len = 0U;
    uart_output[0] = '\0';
}

void uart_putc(char value)
{
    assert(uart_output_len < (sizeof(uart_output) - 1U));
    uart_output[uart_output_len] = value;
    ++uart_output_len;
    uart_output[uart_output_len] = '\0';
}

void uart_puts(const char *text)
{
    while (*text != '\0') {
        uart_putc(*text);
        ++text;
    }
}

void uart_put_hex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    uart_puts("0x");
    for (uint32_t shift = 28U;; shift -= 4U) {
        uart_putc(hex[(value >> shift) & 0x0FUL]);
        if (shift == 0U) {
            break;
        }
    }
}

void uart_put_u32(uint32_t value)
{
    char digits[10];
    uint32_t length = 0U;

    if (value == 0UL) {
        uart_putc('0');
        return;
    }
    while (value != 0UL) {
        digits[length] = (char)('0' + (value % 10UL));
        ++length;
        value /= 10UL;
    }
    while (length != 0U) {
        --length;
        uart_putc(digits[length]);
    }
}

void log_write(
    log_severity_t severity,
    uint16_t source,
    uint16_t event,
    const void *payload,
    uint8_t length
)
{
    (void)severity;
    (void)source;
    (void)payload;
    (void)length;
    last_logged_event = event;
}

uint32_t log_count(void) { return 0UL; }
uint32_t log_dropped(void) { return 0UL; }
uint32_t log_last_sequence(void) { return 0UL; }
int fault_valid(void) { return 0; }
platform_health_state_t platform_health_get_state(void) { return PLATFORM_HEALTHY; }

const experiment_telemetry_report_t *experiment_telemetry_report(void)
{
    static const experiment_telemetry_report_t report = {
        .boot_counter = 1UL,
    };
    return &report;
}

int main(void)
{
    reset_output();
    assert(runtime_monitor_diagnostic_allowed(RSM_INFO_PUBLIC) == 1U);
    assert(runtime_monitor_diagnostic_allowed(RSM_INFO_SECRET) == 0U);

#if RSM_ENABLE == 0U
    rsm_status_snapshot_t status;
    rsm_statistics_t statistics;
    rsm_hardware_info_t hardware;
    rsm_evidence_snapshot_t evidence;

    assert(runtime_monitor_diagnostic_allowed(RSM_INFO_RESTRICTED) == 0U);
    assert(runtime_monitor_event_count() == 0UL);
    runtime_monitor_get_status(&status);
    runtime_monitor_get_statistics(&statistics);
    runtime_monitor_get_hardware_info(&hardware);
    runtime_monitor_get_evidence(&evidence);
    assert(status.schema_version == RSM_SCHEMA_VERSION);
    assert(status.active_slot == RSM_SLOT_NONE);
    assert(statistics.event_count == 0UL);
    assert(statistics.security_counters.diagnostic_denials == 0UL);
    assert(hardware.schema_version == RSM_SCHEMA_VERSION);
    assert(hardware.active_slot == RSM_SLOT_NONE);
    assert(hardware.uid_fingerprint == 0UL);
    assert(hardware.hclk_available == 0U);
    assert(evidence.schema_version == RSM_SCHEMA_VERSION);
    assert(evidence.active_slot == RSM_SLOT_NONE);
    reset_output();
    runtime_monitor_print_public_status();
    assert(strstr(uart_output, "rsm.schema=1\n") != 0);
    assert(strstr(uart_output, "rsm.status=disabled\n") != 0);
#elif RSM_RESTRICTED_DIAGNOSTICS != 0U
    assert(runtime_monitor_diagnostic_allowed(RSM_INFO_RESTRICTED) == 1U);
#else
    assert(runtime_monitor_diagnostic_allowed(RSM_INFO_RESTRICTED) == 0U);
    reset_output();
    assert(runtime_monitor_print_restricted_status() == RSM_STATUS_UNAVAILABLE);
    assert(strstr(uart_output, "rsm.schema=1\n") != 0);
    assert(strstr(uart_output, "rsm.policy.restricted=denied\n") != 0);
    assert(strstr(uart_output, "rsm.device.uid=") == 0);
    assert(strstr(uart_output, "FLASH_OPTCR") == 0);
    assert(strstr(uart_output, "rsm.telemetry.msp") == 0);
    assert(last_logged_event == RSM_EVENT_DIAG_RESTRICTED_DENIED);
#endif

    return 0;
}
