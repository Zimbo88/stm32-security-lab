#include "test_scenarios.h"

#include <stdint.h>

#include "platform.h"
#include "system_reset.h"
#include "uart.h"

void test_scenario_init(void)
{
#if defined(TEST_SCENARIO_trial_success) || \
    defined(TEST_SCENARIO_trial_no_confirm) || \
    defined(TEST_SCENARIO_trial_hardfault) || \
    defined(TEST_SCENARIO_trial_watchdog_hang) || \
    defined(TEST_SCENARIO_trial_software_reset) || \
    defined(TEST_SCENARIO_trial_health_fail) || \
    defined(TEST_SCENARIO_trial_invalid_vector) || \
    defined(TEST_SCENARIO_trial_delayed_confirm)
    uart_puts("TEST_SCENARIO active\n");
#endif
}

void test_scenario_service(uint32_t tick)
{
#if defined(TEST_SCENARIO_trial_watchdog_hang)
    if (tick >= 10UL) {
        uart_puts("TEST_SCENARIO trial_watchdog_hang\n");
        for (;;) {
            __asm volatile("nop");
        }
    }
#elif defined(TEST_SCENARIO_trial_hardfault)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO trial_hardfault\n");
        *(volatile uint32_t *)(uintptr_t)0xFFFFFFF0UL = 0xBADF00DUL;
    }
#elif defined(TEST_SCENARIO_trial_software_reset)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO trial_software_reset\n");
        system_reset_request();
    }
#else
    (void)tick;
#endif
}

uint8_t test_scenario_health_ok(void)
{
#if defined(TEST_SCENARIO_trial_no_confirm) || \
    defined(TEST_SCENARIO_trial_health_fail) || \
    defined(TEST_SCENARIO_trial_hardfault) || \
    defined(TEST_SCENARIO_trial_watchdog_hang) || \
    defined(TEST_SCENARIO_trial_software_reset)
    return 0U;
#else
    return 1U;
#endif
}

uint8_t test_scenario_stable(uint32_t tick)
{
#if defined(TEST_SCENARIO_trial_delayed_confirm)
    return (tick >= 100UL) ? 1U : 0U;
#else
    (void)tick;
    return 1U;
#endif
}
