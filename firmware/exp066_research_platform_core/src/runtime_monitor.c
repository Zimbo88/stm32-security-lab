#include "runtime_monitor.h"

#include "boot_slot.h"
#include "experiment_telemetry.h"
#include "log.h"
#include "platform.h"
#include "platform_confirmation.h"
#include "platform_health.h"
#include "uart.h"

#define REG32(a) (*(volatile uint32_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))

#define CPUID REG32(0xE000ED00UL)
#define CPACR REG32(0xE000ED88UL)
#define VTOR REG32(0xE000ED08UL)
#define MPU_TYPE REG32(0xE000ED90UL)
#define MPU_CTRL REG32(0xE000ED94UL)
#define FLASH_ACR REG32(0x40023C00UL)
#define FLASH_OPTCR REG32(0x40023C14UL)
#define RCC_CFGR REG32(0x40023808UL)
#define UID0 REG32(0x1FFF7A10UL)
#define UID1 REG32(0x1FFF7A14UL)
#define UID2 REG32(0x1FFF7A18UL)
#define FLASH_SIZE_KB REG16(0x1FFF7A22UL)
#define DBGMCU_ID REG32(0xE0042000UL)

#define RSM_SATURATION_DIAGNOSTIC_DENIALS (1UL << 0)
#define RSM_SATURATION_EVENT_LOG_OVERFLOWS (1UL << 1)

#if RSM_ENABLE != 0U
static uint32_t monitor_reset_csr;
static uint8_t monitor_self_tests_passed;
static uint8_t monitor_initialized;
static rsm_security_counters_t security_counters;
#endif

static void kv_str(const char *key, const char *value)
{
    uart_puts(key);
    uart_putc('=');
    uart_puts(value);
    uart_putc('\n');
}

static void kv_u32(const char *key, uint32_t value)
{
    uart_puts(key);
    uart_putc('=');
    uart_put_u32(value);
    uart_putc('\n');
}

#if RSM_ENABLE != 0U
static void kv_hex32(const char *key, uint32_t value)
{
    uart_puts(key);
    uart_putc('=');
    uart_put_hex32(value);
    uart_putc('\n');
}

static void output_putc(void *context, char value)
{
    (void)context;
    uart_putc(value);
}

static void output_puts(void *context, const char *text)
{
    (void)context;
    uart_puts(text);
}
#endif

#if RSM_ENABLE != 0U
static uint32_t runtime_slot(void)
{
#if PLATFORM_APP_BASE == STM32F429_SLOT_A_PAYLOAD_BASE
    return BOOT_SLOT_A;
#elif PLATFORM_APP_BASE == STM32F429_SLOT_B_PAYLOAD_BASE
    return BOOT_SLOT_B;
#else
    return BOOT_SLOT_NONE;
#endif
}

static uint8_t vector_check_passed(void)
{
    return VTOR == PLATFORM_APP_BASE ? 1U : 0U;
}

static uint8_t hclk_hz(uint32_t *value)
{
    return rsm_hclk_hz_from_cfgr(RCC_CFGR, value);
}

static const char *health_name_public(platform_health_state_t state)
{
    switch (state) {
    case PLATFORM_BOOTING:
        return "booting";
    case PLATFORM_HEALTHY:
        return "healthy";
    case PLATFORM_DEGRADED:
        return "degraded";
    case PLATFORM_UPDATING:
        return "updating";
    case PLATFORM_RECOVERY:
        return "recovery";
    case PLATFORM_TEST_RUNNING:
        return "test_running";
    case PLATFORM_FAULT:
        return "fault";
    case PLATFORM_SECURITY_FAILURE:
        return "security_failure";
    default:
        return "unknown";
    }
}

static uint32_t firmware_version_from_telemetry(void)
{
    const experiment_telemetry_report_t *report = experiment_telemetry_report();

    if ((report != 0) && (report->image_version != 0UL)) {
        return report->image_version;
    }
    return 0UL;
}
#endif

static void refresh_event_counter_snapshot(void)
{
#if RSM_ENABLE != 0U
    security_counters.event_log_overflows = log_dropped();
    if (security_counters.event_log_overflows == UINT32_MAX) {
        security_counters.saturation_flags |=
            RSM_SATURATION_EVENT_LOG_OVERFLOWS;
    }
#endif
}

#if RSM_ENABLE != 0U
static log_severity_t map_severity(rsm_severity_t severity)
{
    switch (severity) {
    case RSM_SEVERITY_DEBUG:
        return LOG_DEBUG;
    case RSM_SEVERITY_INFO:
        return LOG_INFO;
    case RSM_SEVERITY_WARNING:
        return LOG_WARNING;
    case RSM_SEVERITY_ERROR:
        return LOG_ERROR;
    case RSM_SEVERITY_CRITICAL:
    default:
        return LOG_CRITICAL;
    }
}
#endif

rsm_status_t runtime_monitor_init(const rsm_config_t *config)
{
#if RSM_ENABLE == 0U
    (void)config;
    return RSM_STATUS_UNAVAILABLE;
#else
    if (config == 0) {
        return RSM_STATUS_INVALID_ARGUMENT;
    }

    monitor_reset_csr = config->reset_csr;
    monitor_self_tests_passed = config->boot_self_tests_passed;
    monitor_initialized = 1U;
    runtime_monitor_log_event(
        RSM_EVENT_EVIDENCE_RUNTIME_READY,
        RSM_SEVERITY_INFO,
        0,
        0U
    );
    return RSM_STATUS_OK;
#endif
}

void runtime_monitor_periodic(void)
{
    refresh_event_counter_snapshot();
}

rsm_status_t runtime_monitor_log_event(
    rsm_event_id_t event_id,
    rsm_severity_t severity,
    const void *payload,
    uint8_t payload_size
)
{
#if RSM_ENABLE == 0U
    (void)event_id;
    (void)severity;
    (void)payload;
    (void)payload_size;
    return RSM_STATUS_UNAVAILABLE;
#else
    if (payload_size > RSM_EVENT_PAYLOAD_SIZE) {
        return RSM_STATUS_INVALID_ARGUMENT;
    }

    log_write(
        map_severity(severity),
        (uint16_t)rsm_event_domain(event_id),
        event_id,
        payload,
        payload_size
    );
    return RSM_STATUS_OK;
#endif
}

void runtime_monitor_get_status(rsm_status_snapshot_t *snapshot)
{
#if RSM_ENABLE == 0U
    if (snapshot == 0) {
        return;
    }

    *snapshot = (rsm_status_snapshot_t){
        .schema_version = RSM_SCHEMA_VERSION,
        .active_slot = RSM_SLOT_NONE
    };
#else
    const experiment_telemetry_report_t *report = experiment_telemetry_report();

    if (snapshot == 0) {
        return;
    }

    snapshot->schema_version = RSM_SCHEMA_VERSION;
    snapshot->boot_sequence = report != 0 ? report->boot_counter : 0UL;
    snapshot->last_event_sequence = log_last_sequence();
    snapshot->reset_csr = monitor_reset_csr;
    snapshot->active_slot = runtime_slot();
    snapshot->firmware_version = firmware_version_from_telemetry();
    snapshot->boot_self_tests_passed = monitor_self_tests_passed;
    snapshot->fault_record_valid = fault_valid() ? 1U : 0U;
    snapshot->vector_check_passed = vector_check_passed();
#endif
}

void runtime_monitor_get_statistics(rsm_statistics_t *statistics)
{
    if (statistics == 0) {
        return;
    }

#if RSM_ENABLE == 0U
    *statistics = (rsm_statistics_t){0};
#else
    statistics->event_count = log_count();
    statistics->event_dropped = log_dropped();
    statistics->last_event_sequence = log_last_sequence();
    refresh_event_counter_snapshot();
    statistics->security_counters = security_counters;
#endif
}

void runtime_monitor_get_hardware_info(rsm_hardware_info_t *information)
{
#if RSM_ENABLE == 0U
    if (information == 0) {
        return;
    }

    *information = (rsm_hardware_info_t){
        .schema_version = RSM_SCHEMA_VERSION,
        .active_slot = RSM_SLOT_NONE
    };
#else
    uint32_t hclk = 0UL;

    if (information == 0) {
        return;
    }

    information->schema_version = RSM_SCHEMA_VERSION;
    information->device_id = DBGMCU_ID & 0x0FFFUL;
    information->revision_id = (DBGMCU_ID >> 16U) & 0xFFFFUL;
    information->flash_kib = (uint32_t)FLASH_SIZE_KB;
    information->cpuid = CPUID;
    information->uid_fingerprint =
        rsm_uid_fingerprint_words(UID0, UID1, UID2);
    information->hclk_available = hclk_hz(&hclk);
    information->hclk_hz = hclk;
    information->active_slot = runtime_slot();
    information->firmware_version = firmware_version_from_telemetry();
    information->fpu_enabled = ((CPACR >> 20U) & 0x0FUL) == 0x0FUL ? 1U : 0U;
    information->mpu_present = (MPU_TYPE & 0x0000FF00UL) != 0UL ? 1U : 0U;
    information->mpu_enabled = (MPU_CTRL & 1UL) != 0UL ? 1U : 0U;
#endif
}

void runtime_monitor_get_evidence(rsm_evidence_snapshot_t *snapshot)
{
#if RSM_ENABLE == 0U
    if (snapshot == 0) {
        return;
    }

    *snapshot = (rsm_evidence_snapshot_t){
        .schema_version = RSM_SCHEMA_VERSION,
        .active_slot = RSM_SLOT_NONE
    };
#else
    rsm_statistics_t statistics;
    const experiment_telemetry_report_t *report = experiment_telemetry_report();

    if (snapshot == 0) {
        return;
    }

    runtime_monitor_get_statistics(&statistics);
    snapshot->schema_version = RSM_SCHEMA_VERSION;
    snapshot->boot_sequence = report != 0 ? report->boot_counter : 0UL;
    snapshot->last_event_sequence = statistics.last_event_sequence;
    snapshot->reset_csr = monitor_reset_csr;
    snapshot->active_slot = runtime_slot();
    snapshot->firmware_version = firmware_version_from_telemetry();
    snapshot->critical_events = 0UL;
    snapshot->boot_self_tests_passed = monitor_self_tests_passed;
    snapshot->flash_integrity_available = 0U;
    snapshot->vector_check_passed = vector_check_passed();
    snapshot->stack_guard_available = 0U;
    snapshot->option_policy_available = 0U;
    snapshot->fault_record_valid = fault_valid() ? 1U : 0U;
#endif
}

uint32_t runtime_monitor_event_count(void)
{
#if RSM_ENABLE == 0U
    return 0UL;
#else
    return log_count();
#endif
}

uint8_t runtime_monitor_diagnostic_allowed(
    rsm_information_class_t information_class
)
{
#if RSM_ENABLE == 0U
    return information_class == RSM_INFO_PUBLIC ? 1U : 0U;
#else
    return rsm_information_allowed(
        information_class,
        RSM_RESTRICTED_DIAGNOSTICS != 0U ? 1U : 0U
    );
#endif
}

void runtime_monitor_record_diagnostic_denial(
    rsm_information_class_t information_class
)
{
#if RSM_ENABLE == 0U
    (void)information_class;
#else
    uint8_t saturated = 0U;

    security_counters.diagnostic_denials = rsm_saturating_increment(
        security_counters.diagnostic_denials,
        &saturated
    );
    if (saturated != 0U) {
        security_counters.saturation_flags |=
            RSM_SATURATION_DIAGNOSTIC_DENIALS;
    }
    runtime_monitor_log_event(
        information_class == RSM_INFO_SECRET
            ? RSM_EVENT_DIAG_SECRET_DENIED
            : RSM_EVENT_DIAG_RESTRICTED_DENIED,
        RSM_SEVERITY_WARNING,
        0,
        0U
    );
#endif
}

void runtime_monitor_print_public_status(void)
{
#if RSM_ENABLE == 0U
    kv_u32("rsm.schema", RSM_SCHEMA_VERSION);
    kv_str("rsm.status", "disabled");
    return;
#else
    rsm_hardware_info_t hardware;
    rsm_evidence_snapshot_t evidence;
    rsm_statistics_t statistics;
    rsm_public_status_output_t output_status;
    const rsm_output_t output = {
        .context = 0,
        .putc = output_putc,
        .puts = output_puts
    };
    const uint32_t firmware_version = firmware_version_from_telemetry();

    runtime_monitor_get_hardware_info(&hardware);
    runtime_monitor_get_evidence(&evidence);
    runtime_monitor_get_statistics(&statistics);

    output_status.schema_version = RSM_SCHEMA_VERSION;
    output_status.status = monitor_initialized != 0U ? "ok" : "unavailable";
    output_status.build_diagnostic_mode =
        RSM_RESTRICTED_DIAGNOSTICS != 0U
            ? "restricted_development"
            : "production_default";
    output_status.device_family = "STM32F429";
    output_status.device_id = hardware.device_id;
    output_status.revision_id = hardware.revision_id;
    output_status.flash_kib = hardware.flash_kib;
    output_status.uid_fingerprint = hardware.uid_fingerprint;
    output_status.cpu_core = "Cortex-M4F";
    output_status.cpu_fpu =
        hardware.fpu_enabled != 0U ? "enabled" : "disabled";
    output_status.cpu_mpu = hardware.mpu_enabled != 0U ? "enabled" :
        (hardware.mpu_present != 0U ? "disabled" : "unavailable");
    output_status.cpu_clock_source = rsm_clock_source_name_from_cfgr(RCC_CFGR);
    output_status.hclk_available = hardware.hclk_available;
    output_status.hclk_hz = hardware.hclk_hz;
    output_status.cpuid = hardware.cpuid;
    output_status.boot_sequence = evidence.boot_sequence;
    output_status.boot_slot = rsm_slot_name(evidence.active_slot);
    output_status.firmware_version_available =
        firmware_version != 0UL ? 1U : 0U;
    output_status.firmware_version = firmware_version;
    output_status.last_reset = rsm_reset_cause_name(evidence.reset_csr);
    output_status.boot_self_tests =
        rsm_bool_pass_fail(evidence.boot_self_tests_passed);
    output_status.security_flash = "unavailable";
    output_status.security_vectors =
        rsm_bool_pass_fail(evidence.vector_check_passed);
    output_status.security_stack = "unavailable";
    output_status.security_option_policy = "unavailable";
    output_status.health_state = health_name_public(platform_health_get_state());
    output_status.last_event_sequence = evidence.last_event_sequence;
    output_status.last_fault =
        evidence.fault_record_valid != 0U ? "valid" : "none";
    output_status.timebase = "relative";
    output_status.boot_context = "verified_launch_assumed_no_mailbox";
    output_status.event_count = statistics.event_count;
    output_status.event_dropped = statistics.event_dropped;
    output_status.diagnostic_denials =
        statistics.security_counters.diagnostic_denials;
    output_status.event_log_overflows =
        statistics.security_counters.event_log_overflows;
    output_status.restricted_policy =
        RSM_RESTRICTED_DIAGNOSTICS != 0U ? "enabled" : "disabled";
    output_status.secret_policy = "never";
    rsm_format_public_status(&output_status, &output);
#endif
}

rsm_status_t runtime_monitor_print_restricted_status(void)
{
#if RSM_ENABLE == 0U
    kv_u32("rsm.schema", RSM_SCHEMA_VERSION);
    kv_str("rsm.status", "disabled");
    return RSM_STATUS_UNAVAILABLE;
#else
    const experiment_telemetry_report_t *report = experiment_telemetry_report();

    if (runtime_monitor_diagnostic_allowed(RSM_INFO_RESTRICTED) == 0U) {
        runtime_monitor_record_diagnostic_denial(RSM_INFO_RESTRICTED);
        kv_u32("rsm.schema", RSM_SCHEMA_VERSION);
        kv_str("rsm.policy.restricted", "denied");
        return RSM_STATUS_UNAVAILABLE;
    }

    runtime_monitor_print_public_status();
    uart_puts("rsm.device.uid=");
    uart_put_hex32(UID0);
    uart_putc(',');
    uart_put_hex32(UID1);
    uart_putc(',');
    uart_put_hex32(UID2);
    uart_putc('\n');
    kv_hex32("rsm.register.vtor", VTOR);
    kv_hex32("rsm.register.flash_acr", FLASH_ACR);
    kv_hex32("rsm.register.flash_optcr", FLASH_OPTCR);
    if (report != 0) {
        kv_hex32("rsm.telemetry.msp", report->msp);
        kv_hex32("rsm.telemetry.psp", report->psp);
        kv_hex32("rsm.telemetry.control", report->control);
        kv_hex32("rsm.telemetry.primask", report->primask);
        kv_hex32("rsm.telemetry.basepri", report->basepri);
        kv_hex32("rsm.telemetry.faultmask", report->faultmask);
    }
    return RSM_STATUS_OK;
#endif
}
