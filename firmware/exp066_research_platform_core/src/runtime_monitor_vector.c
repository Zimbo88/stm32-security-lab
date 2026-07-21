#include "runtime_monitor_vector.h"

static uint8_t is_power_of_two(uint32_t value)
{
    return ((value != 0UL) && ((value & (value - 1UL)) == 0UL)) ? 1U : 0U;
}

static uint8_t config_valid(const rsm_vector_config_t *config)
{
    if ((config == 0) ||
        (config->table == 0) ||
        (config->expected_entries == 0) ||
        (config->entry_kinds == 0) ||
        (config->entry_count == 0UL) ||
        (config->vtor_alignment == 0UL) ||
        (config->msp_alignment == 0UL) ||
        (config->sram_base >= config->sram_end) ||
        (config->executable_flash_base >= config->executable_flash_end)) {
        return 0U;
    }
    if ((is_power_of_two(config->vtor_alignment) == 0U) ||
        (is_power_of_two(config->msp_alignment) == 0U)) {
        return 0U;
    }
    if ((config->expected_vtor & (config->vtor_alignment - 1UL)) != 0UL) {
        return 0U;
    }
    return 1U;
}

static void reset_snapshot(rsm_vector_monitor_t *monitor)
{
    monitor->snapshot = (rsm_vector_snapshot_t){
        .schema_version = RSM_SCHEMA_VERSION,
        .failing_index = RSM_VECTOR_FAILURE_INDEX_NONE,
        .status = RSM_VECTOR_UNINITIALIZED
    };
}

static void mark_baseline_unavailable(rsm_vector_monitor_t *monitor)
{
    reset_snapshot(monitor);
    monitor->snapshot.status = RSM_VECTOR_BASELINE_UNAVAILABLE;
    monitor->snapshot.expected_vtor = monitor->config.expected_vtor;
    monitor->snapshot.failing_index = RSM_VECTOR_FAILURE_INDEX_NONE;
    monitor->snapshot.latched_failure = 1U;
}

static void record_failure(
    rsm_vector_monitor_t *monitor,
    rsm_vector_status_t status,
    uint32_t index,
    uint32_t expected,
    uint32_t observed
)
{
    if (monitor->snapshot.latched_failure == 0U) {
        uint8_t saturated = 0U;
        monitor->snapshot.failure_count = rsm_saturating_increment(
            monitor->snapshot.failure_count,
            &saturated
        );
        (void)saturated;
    }

    monitor->snapshot.status = status;
    monitor->snapshot.failing_index = index;
    monitor->snapshot.expected_value = expected;
    monitor->snapshot.observed_value = observed;
    monitor->snapshot.latched_failure = 1U;
    monitor->current_cycle_failed = 1U;
}

static rsm_vector_status_t validate_vtor(
    const rsm_vector_config_t *config,
    uint32_t current_vtor
)
{
    if ((current_vtor & (config->vtor_alignment - 1UL)) != 0UL) {
        return RSM_VECTOR_ALIGNMENT_FAILURE;
    }
    if (current_vtor != config->expected_vtor) {
        return RSM_VECTOR_VTOR_MISMATCH;
    }
    return RSM_VECTOR_PASS;
}

static uint8_t msp_valid(const rsm_vector_config_t *config, uint32_t msp)
{
    if ((msp <= config->sram_base) ||
        (msp > config->sram_end) ||
        ((msp & (config->msp_alignment - 1UL)) != 0UL)) {
        return 0U;
    }
    return 1U;
}

static uint8_t handler_address_valid(
    const rsm_vector_config_t *config,
    uint32_t handler
)
{
    const uint32_t address = handler & ~1UL;

    if ((handler & 1UL) == 0UL) {
        return 0U;
    }
    if ((address < config->executable_flash_base) ||
        (address >= config->executable_flash_end)) {
        return 0U;
    }
    return 1U;
}

static rsm_vector_status_t validate_entry(
    const rsm_vector_config_t *config,
    uint32_t index,
    uint32_t observed
)
{
    const uint32_t expected = config->expected_entries[index];
    const uint8_t kind = config->entry_kinds[index];

    if (kind == (uint8_t)RSM_VECTOR_ENTRY_RESERVED) {
        return observed == 0UL ? RSM_VECTOR_PASS : RSM_VECTOR_ENTRY_CHANGED;
    }

    if (kind == (uint8_t)RSM_VECTOR_ENTRY_INITIAL_MSP) {
        if (msp_valid(config, observed) == 0U) {
            return RSM_VECTOR_INITIAL_MSP_INVALID;
        }
        return observed == expected ? RSM_VECTOR_PASS : RSM_VECTOR_ENTRY_CHANGED;
    }

    if (kind != (uint8_t)RSM_VECTOR_ENTRY_HANDLER) {
        return RSM_VECTOR_BASELINE_UNAVAILABLE;
    }

    if (observed == 0UL) {
        return RSM_VECTOR_HANDLER_RANGE_FAILURE;
    }
    if ((observed & 1UL) == 0UL) {
        return RSM_VECTOR_THUMB_BIT_FAILURE;
    }
    if (handler_address_valid(config, observed) == 0U) {
        return RSM_VECTOR_HANDLER_RANGE_FAILURE;
    }
    return observed == expected ? RSM_VECTOR_PASS : RSM_VECTOR_ENTRY_CHANGED;
}

static rsm_status_t check_entry(
    rsm_vector_monitor_t *monitor,
    uint32_t index
)
{
    const uint32_t observed = monitor->config.table[index];
    const uint32_t expected = monitor->config.expected_entries[index];
    const rsm_vector_status_t status =
        validate_entry(&monitor->config, index, observed);

    if (status != RSM_VECTOR_PASS) {
        record_failure(monitor, status, index, expected, observed);
        return RSM_STATUS_FAILED;
    }
    return RSM_STATUS_OK;
}

static rsm_status_t check_vtor(
    rsm_vector_monitor_t *monitor,
    uint32_t current_vtor
)
{
    const rsm_vector_status_t status =
        validate_vtor(&monitor->config, current_vtor);

    monitor->snapshot.current_vtor = current_vtor;
    monitor->snapshot.expected_vtor = monitor->config.expected_vtor;

    if (status != RSM_VECTOR_PASS) {
        record_failure(
            monitor,
            status,
            RSM_VECTOR_FAILURE_INDEX_NONE,
            monitor->config.expected_vtor,
            current_vtor
        );
        return RSM_STATUS_FAILED;
    }
    return RSM_STATUS_OK;
}

static void record_successful_complete_check(
    rsm_vector_monitor_t *monitor,
    uint32_t event_sequence
)
{
    uint8_t saturated = 0U;
    monitor->snapshot.check_count = rsm_saturating_increment(
        monitor->snapshot.check_count,
        &saturated
    );
    (void)saturated;

    if (monitor->snapshot.latched_failure == 0U) {
        monitor->snapshot.status = RSM_VECTOR_PASS;
        monitor->snapshot.last_successful_sequence = event_sequence;
    }
    monitor->snapshot.last_check_complete = 1U;
}

rsm_status_t rsm_vector_monitor_init(
    rsm_vector_monitor_t *monitor,
    const rsm_vector_config_t *config,
    uint32_t current_vtor,
    uint32_t event_sequence
)
{
    if (monitor == 0) {
        return RSM_STATUS_INVALID_ARGUMENT;
    }

    monitor->config = (config != 0) ? *config : (rsm_vector_config_t){0};
    monitor->next_index = 0UL;
    monitor->current_cycle_failed = 0U;
    reset_snapshot(monitor);
    monitor->snapshot.current_vtor = current_vtor;
    monitor->snapshot.expected_vtor = monitor->config.expected_vtor;

    if (config_valid(&monitor->config) == 0U) {
        mark_baseline_unavailable(monitor);
        return RSM_STATUS_UNAVAILABLE;
    }

    monitor->snapshot.available = 1U;

    if (check_vtor(monitor, current_vtor) != RSM_STATUS_OK) {
        return RSM_STATUS_FAILED;
    }

    for (uint32_t index = 0UL; index < monitor->config.entry_count; ++index) {
        monitor->snapshot.checked_entries = index + 1UL;
        if (check_entry(monitor, index) != RSM_STATUS_OK) {
            monitor->next_index = index + 1UL;
            if (monitor->next_index >= monitor->config.entry_count) {
                monitor->next_index = 0UL;
            }
            return RSM_STATUS_FAILED;
        }
    }

    monitor->next_index = 0UL;
    monitor->current_cycle_failed = 0U;
    record_successful_complete_check(monitor, event_sequence);
    return RSM_STATUS_OK;
}

rsm_status_t rsm_vector_monitor_step(
    rsm_vector_monitor_t *monitor,
    uint32_t current_vtor,
    uint32_t event_sequence,
    uint32_t entry_budget
)
{
    rsm_status_t result = RSM_STATUS_OK;
    uint32_t checked = 0UL;

    if (monitor == 0) {
        return RSM_STATUS_INVALID_ARGUMENT;
    }
    if (config_valid(&monitor->config) == 0U) {
        mark_baseline_unavailable(monitor);
        return RSM_STATUS_UNAVAILABLE;
    }
    if (entry_budget == 0UL) {
        return RSM_STATUS_BUSY;
    }

    monitor->snapshot.available = 1U;
    monitor->snapshot.last_check_complete = 0U;
    monitor->snapshot.checked_entries = 0UL;

    if (check_vtor(monitor, current_vtor) != RSM_STATUS_OK) {
        return RSM_STATUS_FAILED;
    }

    while ((checked < entry_budget) &&
           (checked < monitor->config.entry_count)) {
        const uint32_t index = monitor->next_index;
        ++checked;
        monitor->snapshot.checked_entries = checked;

        if (check_entry(monitor, index) != RSM_STATUS_OK) {
            result = RSM_STATUS_FAILED;
        }

        ++monitor->next_index;
        if (monitor->next_index >= monitor->config.entry_count) {
            monitor->next_index = 0UL;
            if (monitor->current_cycle_failed == 0U) {
                record_successful_complete_check(monitor, event_sequence);
            } else {
                uint8_t saturated = 0U;
                monitor->snapshot.check_count = rsm_saturating_increment(
                    monitor->snapshot.check_count,
                    &saturated
                );
                (void)saturated;
                monitor->snapshot.last_check_complete = 1U;
            }
            monitor->current_cycle_failed = 0U;
        }
    }

    return result;
}

void rsm_vector_monitor_get_snapshot(
    const rsm_vector_monitor_t *monitor,
    rsm_vector_snapshot_t *snapshot
)
{
    if (snapshot == 0) {
        return;
    }
    if (monitor == 0) {
        *snapshot = (rsm_vector_snapshot_t){
            .schema_version = RSM_SCHEMA_VERSION,
            .failing_index = RSM_VECTOR_FAILURE_INDEX_NONE,
            .status = RSM_VECTOR_UNINITIALIZED
        };
        return;
    }
    *snapshot = monitor->snapshot;
}

const char *rsm_vector_public_status_name(
    const rsm_vector_snapshot_t *snapshot
)
{
    if ((snapshot == 0) ||
        (snapshot->available == 0U) ||
        (snapshot->status == RSM_VECTOR_UNINITIALIZED) ||
        (snapshot->status == RSM_VECTOR_BASELINE_UNAVAILABLE)) {
        return "unavailable";
    }
    if ((snapshot->latched_failure != 0U) ||
        (snapshot->status != RSM_VECTOR_PASS)) {
        return "fail";
    }
    return "pass";
}

const char *rsm_vector_failure_class_name(rsm_vector_status_t status)
{
    switch (status) {
    case RSM_VECTOR_PASS:
        return "none";
    case RSM_VECTOR_VTOR_MISMATCH:
        return "vtor_mismatch";
    case RSM_VECTOR_ALIGNMENT_FAILURE:
        return "alignment_failure";
    case RSM_VECTOR_INITIAL_MSP_INVALID:
        return "initial_msp_invalid";
    case RSM_VECTOR_HANDLER_RANGE_FAILURE:
        return "handler_range_failure";
    case RSM_VECTOR_THUMB_BIT_FAILURE:
        return "thumb_bit_failure";
    case RSM_VECTOR_ENTRY_CHANGED:
        return "entry_changed";
    case RSM_VECTOR_BASELINE_UNAVAILABLE:
        return "baseline_unavailable";
    case RSM_VECTOR_UNINITIALIZED:
    default:
        return "unavailable";
    }
}
