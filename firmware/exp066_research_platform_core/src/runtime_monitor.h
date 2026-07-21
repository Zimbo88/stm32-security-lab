#ifndef RUNTIME_MONITOR_H
#define RUNTIME_MONITOR_H

#include <stdint.h>

#include "runtime_monitor_core.h"
#include "runtime_monitor_vector.h"

#ifndef RSM_ENABLE
#define RSM_ENABLE 1U
#endif

typedef struct {
    uint32_t reset_csr;
    uint8_t boot_self_tests_passed;
} rsm_config_t;

typedef struct {
    uint32_t schema_version;
    uint32_t boot_sequence;
    uint32_t last_event_sequence;
    uint32_t reset_csr;
    uint32_t active_slot;
    uint32_t firmware_version;
    uint8_t boot_self_tests_passed;
    uint8_t fault_record_valid;
    uint8_t vector_check_passed;
    uint8_t vector_monitor_available;
    rsm_vector_status_t vector_monitor_status;
    uint8_t latched_vector_failure;
} rsm_status_snapshot_t;

typedef struct {
    uint32_t schema_version;
    uint32_t device_id;
    uint32_t revision_id;
    uint32_t flash_kib;
    uint32_t cpuid;
    uint32_t uid_fingerprint;
    uint32_t hclk_hz;
    uint32_t active_slot;
    uint32_t firmware_version;
    uint8_t fpu_enabled;
    uint8_t mpu_present;
    uint8_t mpu_enabled;
    uint8_t hclk_available;
} rsm_hardware_info_t;

typedef struct {
    uint32_t schema_version;
    uint32_t boot_sequence;
    uint32_t last_event_sequence;
    uint32_t reset_csr;
    uint32_t active_slot;
    uint32_t firmware_version;
    uint32_t critical_events;
    uint32_t vector_check_count;
    uint32_t vector_failure_count;
    uint32_t vector_last_successful_sequence;
    uint8_t boot_self_tests_passed;
    uint8_t flash_integrity_available;
    uint8_t vector_check_passed;
    uint8_t vector_monitor_available;
    rsm_vector_status_t vector_monitor_status;
    uint8_t latched_vector_failure;
    uint8_t stack_guard_available;
    uint8_t option_policy_available;
    uint8_t fault_record_valid;
} rsm_evidence_snapshot_t;

typedef struct {
    uint32_t event_count;
    uint32_t event_dropped;
    uint32_t last_event_sequence;
    rsm_security_counters_t security_counters;
} rsm_statistics_t;

/**
 * Initialize the runtime monitor from already captured boot context.
 *
 * The monitor does not authenticate the image itself and does not change Flash
 * layout, option bytes, recovery state, or slot selection.
 */
rsm_status_t runtime_monitor_init(const rsm_config_t *config);

/**
 * Service bounded monitor bookkeeping from the normal main loop.
 *
 * Phase 2A advances the vector-table monitor with a fixed entry budget when
 * that module is enabled. Stack and Flash integrity checks are intentionally
 * not implemented here.
 */
void runtime_monitor_periodic(void);

/** Write a typed RSM event into the existing EXP066 RAM event ring. */
rsm_status_t runtime_monitor_log_event(
    rsm_event_id_t event_id,
    rsm_severity_t severity,
    const void *payload,
    uint8_t payload_size
);

/** Copy the current public runtime status snapshot into caller storage. */
void runtime_monitor_get_status(rsm_status_snapshot_t *snapshot);

/** Copy aggregate event and security counters into caller storage. */
void runtime_monitor_get_statistics(rsm_statistics_t *statistics);

/** Copy the public hardware inventory snapshot into caller storage. */
void runtime_monitor_get_hardware_info(rsm_hardware_info_t *information);

/** Copy the runtime evidence summary into caller storage. */
void runtime_monitor_get_evidence(rsm_evidence_snapshot_t *snapshot);

/** Return the number of retained RSM-visible events. */
uint32_t runtime_monitor_event_count(void);

/** Return whether a diagnostic information class may be emitted. */
uint8_t runtime_monitor_diagnostic_allowed(
    rsm_information_class_t information_class
);

/** Record a denied restricted or secret diagnostic request. */
void runtime_monitor_record_diagnostic_denial(
    rsm_information_class_t information_class
);

/** Emit the stable public `rsm.*` key-value status over the active UART. */
void runtime_monitor_print_public_status(void);

/** Emit restricted status if the explicit development build flag allows it. */
rsm_status_t runtime_monitor_print_restricted_status(void);

#endif
