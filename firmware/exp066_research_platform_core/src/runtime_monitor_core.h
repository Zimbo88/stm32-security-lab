#ifndef RUNTIME_MONITOR_CORE_H
#define RUNTIME_MONITOR_CORE_H

#include <stdint.h>

#define RSM_SCHEMA_VERSION 1U
#define RSM_EVENT_PAYLOAD_SIZE 12U
#define RSM_SLOT_NONE 0xFFFFFFFFUL

#ifndef RSM_RESTRICTED_DIAGNOSTICS
#define RSM_RESTRICTED_DIAGNOSTICS 0U
#endif

typedef enum {
    RSM_STATUS_OK = 0,
    RSM_STATUS_DEGRADED,
    RSM_STATUS_FAILED,
    RSM_STATUS_UNAVAILABLE,
    RSM_STATUS_BUSY,
    RSM_STATUS_INVALID_ARGUMENT
} rsm_status_t;

typedef enum {
    RSM_SEVERITY_DEBUG = 0,
    RSM_SEVERITY_INFO,
    RSM_SEVERITY_WARNING,
    RSM_SEVERITY_ERROR,
    RSM_SEVERITY_CRITICAL
} rsm_severity_t;

typedef enum {
    RSM_INFO_PUBLIC = 0,
    RSM_INFO_RESTRICTED,
    RSM_INFO_SECRET
} rsm_information_class_t;

typedef uint16_t rsm_event_id_t;

enum {
    RSM_EVENT_DOMAIN_CORE = 0x0000U,
    RSM_EVENT_DOMAIN_SECURE_BOOT = 0x0100U,
    RSM_EVENT_DOMAIN_UPDATE = 0x0200U,
    RSM_EVENT_DOMAIN_FLASH_INTEGRITY = 0x0300U,
    RSM_EVENT_DOMAIN_RAM_STACK = 0x0400U,
    RSM_EVENT_DOMAIN_VECTOR_TABLE = 0x0500U,
    RSM_EVENT_DOMAIN_OPTION_BYTES = 0x0600U,
    RSM_EVENT_DOMAIN_SELF_TEST = 0x0700U,
    RSM_EVENT_DOMAIN_FAULT = 0x0800U,
    RSM_EVENT_DOMAIN_RESET = 0x0900U,
    RSM_EVENT_DOMAIN_STORAGE = 0x0A00U,
    RSM_EVENT_DOMAIN_HARDWARE_HEALTH = 0x0B00U,
    RSM_EVENT_DOMAIN_DIAGNOSTIC_POLICY = 0x0C00U,
    RSM_EVENT_DOMAIN_EVIDENCE_CHAIN = 0x0D00U
};

enum {
    RSM_EVENT_EVIDENCE_RUNTIME_READY =
        RSM_EVENT_DOMAIN_EVIDENCE_CHAIN | 0x0001U,
    RSM_EVENT_VECTOR_MONITOR_INITIALIZED =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0001U,
    RSM_EVENT_VECTOR_FULL_CYCLE_PASSED =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0002U,
    RSM_EVENT_VECTOR_VTOR_MISMATCH =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0003U,
    RSM_EVENT_VECTOR_INITIAL_MSP_INVALID =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0004U,
    RSM_EVENT_VECTOR_HANDLER_RANGE_FAILURE =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0005U,
    RSM_EVENT_VECTOR_THUMB_BIT_FAILURE =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0006U,
    RSM_EVENT_VECTOR_ENTRY_CHANGED =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0007U,
    RSM_EVENT_VECTOR_BASELINE_UNAVAILABLE =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0008U,
    RSM_EVENT_VECTOR_ALIGNMENT_FAILURE =
        RSM_EVENT_DOMAIN_VECTOR_TABLE | 0x0009U,
    RSM_EVENT_DIAG_RESTRICTED_DENIED =
        RSM_EVENT_DOMAIN_DIAGNOSTIC_POLICY | 0x0001U,
    RSM_EVENT_DIAG_SECRET_DENIED =
        RSM_EVENT_DOMAIN_DIAGNOSTIC_POLICY | 0x0002U,
    RSM_EVENT_RESET_CAUSE_CAPTURED =
        RSM_EVENT_DOMAIN_RESET | 0x0001U,
    RSM_EVENT_WATCHDOG_INITIALIZED =
        RSM_EVENT_DOMAIN_HARDWARE_HEALTH | 0x0001U,
    RSM_EVENT_WATCHDOG_INIT_FAILED =
        RSM_EVENT_DOMAIN_HARDWARE_HEALTH | 0x0002U,
    RSM_EVENT_HEALTH_GATE_PASSED =
        RSM_EVENT_DOMAIN_HARDWARE_HEALTH | 0x0003U,
    RSM_EVENT_HEALTH_GATE_FAILED =
        RSM_EVENT_DOMAIN_HARDWARE_HEALTH | 0x0004U
};

typedef struct {
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t boot_sequence;
    uint16_t event_id;
    uint8_t severity;
    uint8_t flags;
    uint8_t payload[RSM_EVENT_PAYLOAD_SIZE];
    uint32_t crc32;
} rsm_event_record_t;

typedef struct {
    uint32_t boot_failures;
    uint32_t integrity_failures;
    uint32_t signature_failures;
    uint32_t rollback_attempts;
    uint32_t watchdog_resets;
    uint32_t flash_errors;
    uint32_t option_policy_failures;
    uint32_t vector_table_changes;
    uint32_t ram_guard_failures;
    uint32_t fault_count;
    uint32_t event_log_overflows;
    uint32_t diagnostic_denials;
    uint32_t saturation_flags;
} rsm_security_counters_t;

typedef struct {
    void *context;
    void (*putc)(void *context, char value);
    void (*puts)(void *context, const char *text);
} rsm_output_t;

typedef struct {
    uint32_t schema_version;
    const char *status;
    const char *build_diagnostic_mode;
    const char *device_family;
    uint32_t device_id;
    uint32_t revision_id;
    uint32_t flash_kib;
    uint32_t uid_fingerprint;
    const char *cpu_core;
    const char *cpu_fpu;
    const char *cpu_mpu;
    const char *cpu_clock_source;
    uint8_t hclk_available;
    uint32_t hclk_hz;
    uint32_t cpuid;
    uint32_t boot_sequence;
    const char *boot_slot;
    uint8_t firmware_version_available;
    uint32_t firmware_version;
    const char *last_reset;
    const char *boot_self_tests;
    const char *security_flash;
    const char *security_vectors;
    const char *security_stack;
    const char *security_option_policy;
    const char *vector_status;
    const char *vector_failure_class;
    uint32_t vector_check_count;
    uint32_t vector_failure_count;
    const char *vector_latched_failure;
    const char *health_state;
    uint32_t last_event_sequence;
    const char *last_fault;
    const char *timebase;
    const char *boot_context;
    uint32_t event_count;
    uint32_t event_dropped;
    uint32_t diagnostic_denials;
    uint32_t event_log_overflows;
    const char *restricted_policy;
    const char *secret_policy;
} rsm_public_status_output_t;

uint8_t rsm_information_allowed(
    rsm_information_class_t information_class,
    uint8_t restricted_enabled
);
uint32_t rsm_event_domain(rsm_event_id_t event_id);
uint32_t rsm_crc32_update(
    uint32_t crc,
    const uint8_t *data,
    uint32_t length
);
uint32_t rsm_uid_fingerprint_words(
    uint32_t uid0,
    uint32_t uid1,
    uint32_t uid2
);
const char *rsm_reset_cause_name(uint32_t reset_csr);
const char *rsm_slot_name(uint32_t slot);
const char *rsm_bool_pass_fail(uint8_t passed);
const char *rsm_status_name(rsm_status_t status);
uint32_t rsm_saturating_increment(uint32_t value, uint8_t *saturated);
const char *rsm_clock_source_name_from_cfgr(uint32_t cfgr);
uint8_t rsm_hclk_hz_from_cfgr(uint32_t cfgr, uint32_t *hclk_hz);
void rsm_format_public_status(
    const rsm_public_status_output_t *status,
    const rsm_output_t *output
);

#endif
