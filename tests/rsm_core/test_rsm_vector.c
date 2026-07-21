#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "runtime_monitor_vector.h"

#define SLOT_A_BASE 0x08020200UL
#define SLOT_A_END  0x08080000UL
#define SLOT_B_BASE 0x08080200UL
#define SLOT_B_END  0x080E0000UL
#define SRAM_BASE   0x20000000UL
#define SRAM_END    0x20020000UL

static const uint8_t kinds[RSM_VECTOR_TABLE_ENTRY_COUNT] = {
    (uint8_t)RSM_VECTOR_ENTRY_INITIAL_MSP,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_RESERVED,
    (uint8_t)RSM_VECTOR_ENTRY_RESERVED,
    (uint8_t)RSM_VECTOR_ENTRY_RESERVED,
    (uint8_t)RSM_VECTOR_ENTRY_RESERVED,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_RESERVED,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER,
    (uint8_t)RSM_VECTOR_ENTRY_HANDLER
};

static void fill_expected(uint32_t *expected, uint32_t base)
{
    expected[0] = SRAM_END;
    expected[1] = base + 0x101UL;
    expected[2] = base + 0x201UL;
    expected[3] = base + 0x301UL;
    expected[4] = base + 0x401UL;
    expected[5] = base + 0x501UL;
    expected[6] = base + 0x601UL;
    expected[7] = 0UL;
    expected[8] = 0UL;
    expected[9] = 0UL;
    expected[10] = 0UL;
    expected[11] = base + 0x201UL;
    expected[12] = base + 0x201UL;
    expected[13] = 0UL;
    expected[14] = base + 0x201UL;
    expected[15] = base + 0x201UL;
}

static rsm_vector_config_t make_config(
    uint32_t *table,
    const uint32_t *expected,
    uint32_t base,
    uint32_t end
)
{
    return (rsm_vector_config_t){
        .expected_vtor = base,
        .vtor_alignment = 0x100UL,
        .sram_base = SRAM_BASE,
        .sram_end = SRAM_END,
        .msp_alignment = 8UL,
        .executable_flash_base = base,
        .executable_flash_end = end,
        .entry_count = RSM_VECTOR_TABLE_ENTRY_COUNT,
        .table = table,
        .expected_entries = expected,
        .entry_kinds = kinds
    };
}

static void assert_init_status(
    uint32_t *table,
    uint32_t *expected,
    uint32_t base,
    uint32_t end,
    uint32_t vtor,
    rsm_vector_status_t status
)
{
    rsm_vector_monitor_t monitor;
    rsm_vector_snapshot_t snapshot;
    rsm_vector_config_t config;

    config = make_config(table, expected, base, end);
    (void)rsm_vector_monitor_init(&monitor, &config, vtor, 7UL);
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.status == status);
}

static void test_valid_slot_a_and_slot_b(void)
{
    uint32_t expected[RSM_VECTOR_TABLE_ENTRY_COUNT];
    uint32_t table[RSM_VECTOR_TABLE_ENTRY_COUNT];
    rsm_vector_monitor_t monitor;
    rsm_vector_snapshot_t snapshot;
    rsm_vector_config_t config;

    fill_expected(expected, SLOT_A_BASE);
    memcpy(table, expected, sizeof(table));
    config = make_config(table, expected, SLOT_A_BASE, SLOT_A_END);
    assert(rsm_vector_monitor_init(&monitor, &config, SLOT_A_BASE, 9UL) ==
           RSM_STATUS_OK);
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.available == 1U);
    assert(snapshot.status == RSM_VECTOR_PASS);
    assert(snapshot.checked_entries == RSM_VECTOR_TABLE_ENTRY_COUNT);
    assert(snapshot.check_count == 1UL);
    assert(snapshot.last_successful_sequence == 9UL);

    fill_expected(expected, SLOT_B_BASE);
    memcpy(table, expected, sizeof(table));
    config = make_config(table, expected, SLOT_B_BASE, SLOT_B_END);
    assert(rsm_vector_monitor_init(&monitor, &config, SLOT_B_BASE, 10UL) ==
           RSM_STATUS_OK);
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.status == RSM_VECTOR_PASS);
}

static void test_vtor_and_msp_failures(void)
{
    uint32_t expected[RSM_VECTOR_TABLE_ENTRY_COUNT];
    uint32_t table[RSM_VECTOR_TABLE_ENTRY_COUNT];

    fill_expected(expected, SLOT_A_BASE);
    memcpy(table, expected, sizeof(table));
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE + 0x100UL,
        RSM_VECTOR_VTOR_MISMATCH
    );
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE + 4UL,
        RSM_VECTOR_ALIGNMENT_FAILURE
    );

    table[0] = SRAM_END + 8UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_INITIAL_MSP_INVALID
    );
    table[0] = SRAM_END - 4UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_INITIAL_MSP_INVALID
    );
}

static void test_handler_rules(void)
{
    uint32_t expected[RSM_VECTOR_TABLE_ENTRY_COUNT];
    uint32_t table[RSM_VECTOR_TABLE_ENTRY_COUNT];

    fill_expected(expected, SLOT_A_BASE);
    memcpy(table, expected, sizeof(table));
    table[1] &= ~1UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_THUMB_BIT_FAILURE
    );

    memcpy(table, expected, sizeof(table));
    table[2] = (SLOT_A_BASE - 4UL) | 1UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_HANDLER_RANGE_FAILURE
    );

    memcpy(table, expected, sizeof(table));
    table[2] = SLOT_A_END | 1UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_HANDLER_RANGE_FAILURE
    );

    memcpy(table, expected, sizeof(table));
    table[2] = 0UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_HANDLER_RANGE_FAILURE
    );
}

static void test_reserved_default_and_entry_changes(void)
{
    uint32_t expected[RSM_VECTOR_TABLE_ENTRY_COUNT];
    uint32_t table[RSM_VECTOR_TABLE_ENTRY_COUNT];

    fill_expected(expected, SLOT_A_BASE);
    memcpy(table, expected, sizeof(table));
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_PASS
    );

    table[7] = SLOT_A_BASE + 0x701UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_ENTRY_CHANGED
    );

    memcpy(table, expected, sizeof(table));
    table[2] = SLOT_A_BASE + 0x701UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_ENTRY_CHANGED
    );

    memcpy(table, expected, sizeof(table));
    table[1] = SLOT_A_BASE + 0x701UL;
    assert_init_status(
        table,
        expected,
        SLOT_A_BASE,
        SLOT_A_END,
        SLOT_A_BASE,
        RSM_VECTOR_ENTRY_CHANGED
    );
}

static void test_steps_latching_and_saturation(void)
{
    uint32_t expected[RSM_VECTOR_TABLE_ENTRY_COUNT];
    uint32_t table[RSM_VECTOR_TABLE_ENTRY_COUNT];
    rsm_vector_monitor_t monitor;
    rsm_vector_snapshot_t snapshot;
    rsm_vector_config_t config;

    fill_expected(expected, SLOT_A_BASE);
    memcpy(table, expected, sizeof(table));
    config = make_config(table, expected, SLOT_A_BASE, SLOT_A_END);
    assert(rsm_vector_monitor_init(&monitor, &config, SLOT_A_BASE, 1UL) ==
           RSM_STATUS_OK);
    assert(rsm_vector_monitor_step(&monitor, SLOT_A_BASE, 2UL, 1UL) ==
           RSM_STATUS_OK);
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.checked_entries == 1UL);
    assert(snapshot.check_count == 1UL);

    for (uint32_t step = 0UL; step < 8UL; ++step) {
        assert(rsm_vector_monitor_step(&monitor, SLOT_A_BASE, 3UL, 2UL) ==
               RSM_STATUS_OK);
    }
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.check_count >= 2UL);
    assert(snapshot.latched_failure == 0U);

    table[3] = SLOT_A_BASE + 0x711UL;
    for (uint32_t step = 0UL; step < 8UL; ++step) {
        (void)rsm_vector_monitor_step(&monitor, SLOT_A_BASE, 4UL, 2UL);
    }
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.latched_failure == 1U);
    assert(snapshot.status == RSM_VECTOR_ENTRY_CHANGED);
    assert(snapshot.failure_count == 1UL);

    table[3] = expected[3];
    for (uint32_t step = 0UL; step < 8UL; ++step) {
        (void)rsm_vector_monitor_step(&monitor, SLOT_A_BASE, 5UL, 2UL);
    }
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.latched_failure == 1U);
    assert(snapshot.status == RSM_VECTOR_ENTRY_CHANGED);

    monitor.snapshot.latched_failure = 0U;
    monitor.snapshot.failure_count = UINT32_MAX - 1UL;
    table[4] = SLOT_A_BASE + 0x721UL;
    for (uint32_t step = 0UL; step < 8UL; ++step) {
        (void)rsm_vector_monitor_step(&monitor, SLOT_A_BASE, 6UL, 2UL);
    }
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.failure_count == UINT32_MAX);
}

static void test_baseline_unavailable_and_names(void)
{
    rsm_vector_monitor_t monitor;
    rsm_vector_snapshot_t snapshot;

    assert(rsm_vector_monitor_init(&monitor, 0, SLOT_A_BASE, 1UL) ==
           RSM_STATUS_UNAVAILABLE);
    rsm_vector_monitor_get_snapshot(&monitor, &snapshot);
    assert(snapshot.status == RSM_VECTOR_BASELINE_UNAVAILABLE);
    assert(strcmp(rsm_vector_public_status_name(&snapshot), "unavailable") == 0);
    assert(
        strcmp(
            rsm_vector_failure_class_name(RSM_VECTOR_THUMB_BIT_FAILURE),
            "thumb_bit_failure"
        ) == 0
    );
    snapshot.available = 1U;
    snapshot.status = RSM_VECTOR_PASS;
    snapshot.latched_failure = 0U;
    assert(strcmp(rsm_vector_public_status_name(&snapshot), "pass") == 0);
    snapshot.latched_failure = 1U;
    assert(strcmp(rsm_vector_public_status_name(&snapshot), "fail") == 0);
}

int main(void)
{
    test_valid_slot_a_and_slot_b();
    test_vtor_and_msp_failures();
    test_handler_rules();
    test_reserved_default_and_entry_changes();
    test_steps_latching_and_saturation();
    test_baseline_unavailable_and_names();
    return 0;
}
