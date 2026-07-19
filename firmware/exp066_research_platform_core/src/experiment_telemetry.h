#ifndef EXPERIMENT_TELEMETRY_H
#define EXPERIMENT_TELEMETRY_H

#include <stdint.h>

#include "boot_metadata.h"

#define EXPERIMENT_TELEMETRY_FORMAT_VERSION 1UL
#define EXPERIMENT_TELEMETRY_MAGIC0 'S'
#define EXPERIMENT_TELEMETRY_MAGIC1 'T'
#define EXPERIMENT_TELEMETRY_MAGIC2 'M'
#define EXPERIMENT_TELEMETRY_MAGIC3 'R'

#define EXPERIMENT_TELEMETRY_INTEGRITY_NOT_RUN 0UL
#define EXPERIMENT_TELEMETRY_CHANGED_REGION_NONE 0xFFFFFFFFUL

#define EXPERIMENT_TELEMETRY_TRUSTED_METADATA_VALID (1UL << 0)
#define EXPERIMENT_TELEMETRY_TRUSTED_METADATA_AMBIGUOUS (1UL << 1)
#define EXPERIMENT_TELEMETRY_TRUSTED_METADATA_UNRECOVERABLE (1UL << 2)

#define EXPERIMENT_TELEMETRY_OBS_VTOR_EXPECTED (1UL << 0)
#define EXPERIMENT_TELEMETRY_OBS_MSP_IN_RANGE (1UL << 1)
#define EXPERIMENT_TELEMETRY_OBS_PSP_IN_RANGE (1UL << 2)

typedef struct {
    uint8_t magic[4];
    uint32_t format_version;
    uint32_t boot_counter;
    uint32_t reset_cause;
    uint32_t selected_slot;
    uint32_t confirmed_slot;
    uint32_t candidate_slot;
    uint32_t metadata_state;
    uint32_t metadata_sequence;
    uint32_t remaining_trial_attempts;
    uint32_t image_version;
    uint32_t vtor;
    uint32_t msp;
    uint32_t psp;
    uint32_t control;
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    uint32_t integrity_scan_status;
    uint32_t first_changed_region;
    uint32_t last_boot_policy_result;
    uint32_t last_update_result;
    uint32_t trusted_state_flags;
    uint32_t untrusted_observation_flags;
    uint32_t commit_marker;
    uint32_t report_crc;
} experiment_telemetry_report_t;

void experiment_telemetry_init(uint32_t reset_cause_snapshot);
void experiment_telemetry_set_boot_policy_result(uint32_t result);
void experiment_telemetry_set_confirmation_result(uint32_t result);
void experiment_telemetry_refresh_metadata(void);
const experiment_telemetry_report_t *experiment_telemetry_report(void);
uint8_t experiment_telemetry_report_valid(const experiment_telemetry_report_t *report);
void experiment_telemetry_print(void);

#endif
