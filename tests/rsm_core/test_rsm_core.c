#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "runtime_monitor_core.h"

typedef struct {
    char data[4096];
    uint32_t length;
} output_buffer_t;

static void capture_putc(void *context, char value)
{
    output_buffer_t *buffer = (output_buffer_t *)context;

    assert(buffer->length < (sizeof(buffer->data) - 1U));
    buffer->data[buffer->length] = value;
    ++buffer->length;
    buffer->data[buffer->length] = '\0';
}

static void capture_puts(void *context, const char *text)
{
    while (*text != '\0') {
        capture_putc(context, *text);
        ++text;
    }
}

static uint32_t key_length(const char *line)
{
    uint32_t length = 0U;

    while ((line[length] != '\0') &&
           (line[length] != '=') &&
           (line[length] != '\n')) {
        ++length;
    }
    return length;
}

static void assert_key_once(const char *output, const char *key)
{
    uint32_t count = 0U;
    const char *cursor = output;
    const uint32_t expected_length = (uint32_t)strlen(key);

    while (*cursor != '\0') {
        const uint32_t current_length = key_length(cursor);
        if ((current_length == expected_length) &&
            (strncmp(cursor, key, expected_length) == 0) &&
            (cursor[current_length] == '=')) {
            ++count;
        }
        while ((*cursor != '\0') && (*cursor != '\n')) {
            ++cursor;
        }
        if (*cursor == '\n') {
            ++cursor;
        }
    }

    assert(count == 1U);
}

static void assert_lines_are_key_value(const char *output)
{
    const char *cursor = output;

    while (*cursor != '\0') {
        uint8_t saw_key = 0U;
        uint8_t saw_equals = 0U;
        while ((*cursor != '\0') && (*cursor != '\n')) {
            if (*cursor == '=') {
                saw_equals = 1U;
            } else if (saw_equals == 0U) {
                saw_key = 1U;
            }
            ++cursor;
        }
        assert(saw_key != 0U);
        assert(saw_equals != 0U);
        if (*cursor == '\n') {
            ++cursor;
        }
    }
}

static void assert_no_duplicate_keys(const char *output)
{
    const char *left = output;

    while (*left != '\0') {
        const uint32_t left_length = key_length(left);
        const char *right = left;

        while ((*right != '\0') && (*right != '\n')) {
            ++right;
        }
        if (*right == '\n') {
            ++right;
        }

        while (*right != '\0') {
            const uint32_t right_length = key_length(right);
            assert(
                !((left_length == right_length) &&
                  (strncmp(left, right, left_length) == 0) &&
                  (left[left_length] == '=') &&
                  (right[right_length] == '='))
            );
            while ((*right != '\0') && (*right != '\n')) {
                ++right;
            }
            if (*right == '\n') {
                ++right;
            }
        }

        while ((*left != '\0') && (*left != '\n')) {
            ++left;
        }
        if (*left == '\n') {
            ++left;
        }
    }
}

static void test_public_formatter(void)
{
    output_buffer_t buffer = { .data = {0}, .length = 0U };
    const rsm_output_t output = {
        .context = &buffer,
        .putc = capture_putc,
        .puts = capture_puts,
    };
    const rsm_public_status_output_t status = {
        .schema_version = RSM_SCHEMA_VERSION,
        .status = "ok",
        .build_diagnostic_mode = "production_default",
        .device_family = "STM32F429",
        .device_id = 0x419UL,
        .revision_id = 0x1003UL,
        .flash_kib = 1024UL,
        .uid_fingerprint = 0x0E695144UL,
        .cpu_core = "Cortex-M4F",
        .cpu_fpu = "enabled",
        .cpu_mpu = "disabled",
        .cpu_clock_source = "pll",
        .hclk_available = 0U,
        .hclk_hz = 0UL,
        .cpuid = 0x410FC241UL,
        .boot_sequence = 7UL,
        .boot_slot = "A",
        .firmware_version_available = 0U,
        .firmware_version = 0UL,
        .last_reset = "software",
        .boot_self_tests = "pass",
        .security_flash = "unavailable",
        .security_vectors = "pass",
        .security_stack = "unavailable",
        .security_option_policy = "unavailable",
        .vector_status = "pass",
        .vector_failure_class = "none",
        .vector_check_count = 4UL,
        .vector_failure_count = 0UL,
        .vector_latched_failure = "no",
        .health_state = "healthy",
        .last_event_sequence = 9UL,
        .last_fault = "none",
        .timebase = "relative",
        .boot_context = "verified_launch_assumed_no_mailbox",
        .event_count = 3UL,
        .event_dropped = 0UL,
        .diagnostic_denials = 1UL,
        .event_log_overflows = 0UL,
        .restricted_policy = "disabled",
        .secret_policy = "never",
    };

    rsm_format_public_status(&status, &output);
    assert_lines_are_key_value(buffer.data);
    assert_no_duplicate_keys(buffer.data);
    assert_key_once(buffer.data, "rsm.schema");
    assert_key_once(buffer.data, "rsm.build.diagnostic_mode");
    assert_key_once(buffer.data, "rsm.device.uid_fingerprint");
    assert_key_once(buffer.data, "rsm.cpu.hclk_hz");
    assert_key_once(buffer.data, "rsm.boot.firmware_version");
    assert_key_once(buffer.data, "rsm.evidence.boot_context");
    assert_key_once(buffer.data, "rsm.vector.status");
    assert_key_once(buffer.data, "rsm.vector.failure_class");
    assert_key_once(buffer.data, "rsm.vector.checks");
    assert_key_once(buffer.data, "rsm.vector.failures");
    assert_key_once(buffer.data, "rsm.vector.latched_failure");
    assert(strstr(buffer.data, "rsm.cpu.hclk_hz=unavailable\n") != 0);
    assert(strstr(buffer.data, "rsm.boot.firmware_version=unavailable\n") != 0);
    assert(strstr(buffer.data, "rsm.vector.status=pass\n") != 0);
    assert(strstr(buffer.data, "rsm.vector.checks=4\n") != 0);
    assert(strstr(buffer.data, "rsm.device.uid=") == 0);
    assert(strstr(buffer.data, "FLASH_OPTCR") == 0);
    assert(strstr(buffer.data, "rsm.register.") == 0);
    assert(strstr(buffer.data, "rsm.telemetry.msp") == 0);
    assert(strstr(buffer.data, "rsm.vector.expected") == 0);
    assert(strstr(buffer.data, "rsm.vector.observed") == 0);
}

int main(void)
{
    uint8_t saturated = 0U;
    uint32_t hclk = 0UL;

    assert(rsm_information_allowed(RSM_INFO_PUBLIC, 0U) == 1U);
    assert(rsm_information_allowed(RSM_INFO_RESTRICTED, 0U) == 0U);
    assert(rsm_information_allowed(RSM_INFO_RESTRICTED, 1U) == 1U);
    assert(rsm_information_allowed(RSM_INFO_SECRET, 0U) == 0U);
    assert(rsm_information_allowed(RSM_INFO_SECRET, 1U) == 0U);

    assert(rsm_event_domain(0x0D42U) == RSM_EVENT_DOMAIN_EVIDENCE_CHAIN);
    assert(rsm_event_domain(0x0810U) == RSM_EVENT_DOMAIN_FAULT);
    assert(
        rsm_event_domain(RSM_EVENT_EVIDENCE_RUNTIME_READY) ==
        RSM_EVENT_DOMAIN_EVIDENCE_CHAIN
    );
    assert(
        rsm_event_domain(RSM_EVENT_DIAG_RESTRICTED_DENIED) ==
        RSM_EVENT_DOMAIN_DIAGNOSTIC_POLICY
    );
    assert(
        rsm_event_domain(RSM_EVENT_VECTOR_ENTRY_CHANGED) ==
        RSM_EVENT_DOMAIN_VECTOR_TABLE
    );
    assert(
        rsm_event_domain(RSM_EVENT_VECTOR_FULL_CYCLE_PASSED) ==
        RSM_EVENT_DOMAIN_VECTOR_TABLE
    );

    assert(
        rsm_uid_fingerprint_words(
            0x12345678UL,
            0x9ABCDEF0UL,
            0x0FEDCBA9UL
        ) == 0x0E695144UL
    );

    assert(strcmp(rsm_reset_cause_name((1UL << 29) | (1UL << 28)), "iwdg") == 0);
    assert(strcmp(rsm_reset_cause_name((1UL << 30) | (1UL << 28)), "wwdg") == 0);
    assert(strcmp(rsm_reset_cause_name((1UL << 28) | (1UL << 27)), "software") == 0);
    assert(strcmp(rsm_reset_cause_name(1UL << 29), "iwdg") == 0);
    assert(strcmp(rsm_reset_cause_name(1UL << 30), "wwdg") == 0);
    assert(strcmp(rsm_reset_cause_name(1UL << 28), "software") == 0);
    assert(strcmp(rsm_reset_cause_name(1UL << 27), "power_on") == 0);
    assert(strcmp(rsm_reset_cause_name(0UL), "unknown") == 0);

    assert(strcmp(rsm_slot_name(0UL), "A") == 0);
    assert(strcmp(rsm_slot_name(1UL), "B") == 0);
    assert(strcmp(rsm_slot_name(RSM_SLOT_NONE), "unavailable") == 0);

    assert(rsm_saturating_increment(41UL, &saturated) == 42UL);
    assert(saturated == 0U);
    assert(rsm_saturating_increment(UINT32_MAX - 1UL, &saturated) == UINT32_MAX);
    assert(saturated == 1U);
    assert(rsm_saturating_increment(UINT32_MAX, &saturated) == UINT32_MAX);
    assert(saturated == 1U);

    assert(strcmp(rsm_clock_source_name_from_cfgr(0UL), "hsi") == 0);
    assert(strcmp(rsm_clock_source_name_from_cfgr(1UL << 2), "hse") == 0);
    assert(strcmp(rsm_clock_source_name_from_cfgr(2UL << 2), "pll") == 0);
    assert(strcmp(rsm_clock_source_name_from_cfgr(3UL << 2), "unavailable") == 0);
    assert(rsm_hclk_hz_from_cfgr(0UL, &hclk) == 1U);
    assert(hclk == 16000000UL);
    assert(rsm_hclk_hz_from_cfgr(0x8UL << 4, &hclk) == 1U);
    assert(hclk == 8000000UL);
    assert(rsm_hclk_hz_from_cfgr(1UL << 2, &hclk) == 0U);
    assert(hclk == 0UL);
    assert(rsm_hclk_hz_from_cfgr(0UL, 0) == 0U);

    test_public_formatter();

    return 0;
}
