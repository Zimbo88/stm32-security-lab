#ifndef PLATFORM_CONFIRMATION_H
#define PLATFORM_CONFIRMATION_H

#include <stdint.h>

#include "boot_confirmation.h"
#include "boot_metadata.h"

typedef enum {
    PLATFORM_CONFIRMATION_OK = 0,
    PLATFORM_CONFIRMATION_NOT_HEALTHY = 1,
    PLATFORM_CONFIRMATION_SLOT = 2,
    PLATFORM_CONFIRMATION_FLASH = 3,
    PLATFORM_CONFIRMATION_METADATA = 4,
    PLATFORM_CONFIRMATION_AMBIGUOUS_METADATA = 5,
    PLATFORM_CONFIRMATION_NOT_PENDING = 6,
    PLATFORM_CONFIRMATION_COMMIT = 7
} platform_confirmation_status_t;

typedef struct {
    uint8_t early_platform_init_done;
    uint8_t running_slot_identified;
    uint8_t core_self_checks_passed;
    uint8_t critical_initialization_failure;
    uint8_t stable_execution_point_reached;
    uint8_t metadata_allows_confirmation;
    uint8_t uart_diagnostics_ready;
    uint8_t runtime_monitor_ready;
    uint8_t watchdog_active;
    uint8_t application_health_ok;
} platform_confirmation_health_t;

typedef struct {
    uint32_t running_slot;
    uint32_t metadata_state;
    uint32_t metadata_sequence;
    uint32_t confirmed_slot;
    uint32_t candidate_slot;
    uint32_t remaining_trial_attempts;
    uint32_t image_version;
    uint8_t metadata_allows_confirmation;
    uint8_t already_confirmed;
    platform_confirmation_status_t last_status;
    boot_confirm_status_t confirm_status;
    boot_metadata_status_t metadata_status;
} platform_confirmation_snapshot_t;

uint8_t platform_confirmation_health_gate(
    const platform_confirmation_health_t *health
);
platform_confirmation_status_t platform_confirmation_service(
    uint32_t vector_address,
    const platform_confirmation_health_t *base_health
);
void platform_confirmation_snapshot(platform_confirmation_snapshot_t *snapshot);
const char *platform_confirmation_status_text(
    platform_confirmation_status_t status
);

#endif
