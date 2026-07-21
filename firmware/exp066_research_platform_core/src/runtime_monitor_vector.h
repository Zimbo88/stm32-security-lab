#ifndef RUNTIME_MONITOR_VECTOR_H
#define RUNTIME_MONITOR_VECTOR_H

#include <stdint.h>

#include "runtime_monitor_core.h"

#ifndef RSM_VECTOR_MONITOR_ENABLE
#define RSM_VECTOR_MONITOR_ENABLE 1U
#endif

#define RSM_VECTOR_TABLE_ENTRY_COUNT 16U
#define RSM_VECTOR_FAILURE_INDEX_NONE UINT32_MAX
#define RSM_VECTOR_DEFAULT_STEP_BUDGET 2U

typedef enum {
    RSM_VECTOR_ENTRY_RESERVED = 0,
    RSM_VECTOR_ENTRY_INITIAL_MSP,
    RSM_VECTOR_ENTRY_HANDLER
} rsm_vector_entry_kind_t;

typedef enum {
    RSM_VECTOR_UNINITIALIZED = 0,
    RSM_VECTOR_PASS,
    RSM_VECTOR_VTOR_MISMATCH,
    RSM_VECTOR_ALIGNMENT_FAILURE,
    RSM_VECTOR_INITIAL_MSP_INVALID,
    RSM_VECTOR_HANDLER_RANGE_FAILURE,
    RSM_VECTOR_THUMB_BIT_FAILURE,
    RSM_VECTOR_ENTRY_CHANGED,
    RSM_VECTOR_BASELINE_UNAVAILABLE
} rsm_vector_status_t;

typedef struct {
    uint32_t expected_vtor;
    uint32_t vtor_alignment;
    uint32_t sram_base;
    uint32_t sram_end;
    uint32_t msp_alignment;
    uint32_t executable_flash_base;
    uint32_t executable_flash_end;
    uint32_t entry_count;
    const uint32_t *table;
    const uint32_t *expected_entries;
    const uint8_t *entry_kinds;
} rsm_vector_config_t;

typedef struct {
    uint32_t schema_version;
    uint32_t current_vtor;
    uint32_t expected_vtor;
    uint32_t checked_entries;
    uint32_t failing_index;
    uint32_t expected_value;
    uint32_t observed_value;
    uint32_t last_successful_sequence;
    uint32_t check_count;
    uint32_t failure_count;
    rsm_vector_status_t status;
    uint8_t available;
    uint8_t last_check_complete;
    uint8_t latched_failure;
} rsm_vector_snapshot_t;

typedef struct {
    rsm_vector_config_t config;
    rsm_vector_snapshot_t snapshot;
    uint32_t next_index;
    uint8_t current_cycle_failed;
} rsm_vector_monitor_t;

rsm_status_t rsm_vector_monitor_init(
    rsm_vector_monitor_t *monitor,
    const rsm_vector_config_t *config,
    uint32_t current_vtor,
    uint32_t event_sequence
);

rsm_status_t rsm_vector_monitor_step(
    rsm_vector_monitor_t *monitor,
    uint32_t current_vtor,
    uint32_t event_sequence,
    uint32_t entry_budget
);

void rsm_vector_monitor_get_snapshot(
    const rsm_vector_monitor_t *monitor,
    rsm_vector_snapshot_t *snapshot
);

const char *rsm_vector_public_status_name(
    const rsm_vector_snapshot_t *snapshot
);
const char *rsm_vector_failure_class_name(rsm_vector_status_t status);

#endif
