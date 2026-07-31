#include "test_scenarios.h"

#include <stdint.h>

#include "platform.h"
#include "mpu_policy.h"
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
    defined(TEST_SCENARIO_trial_delayed_confirm) || \
    defined(TEST_SCENARIO_mpu_null_access) || \
    defined(TEST_SCENARIO_mpu_execute_sram) || \
    defined(TEST_SCENARIO_mpu_write_bootloader) || \
    defined(TEST_SCENARIO_mpu_write_metadata) || \
    defined(TEST_SCENARIO_mpu_stack_guard) || \
    defined(TEST_SCENARIO_mpu_valid_application)
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
#elif defined(TEST_SCENARIO_mpu_null_access)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO mpu_null_access\n");
        *(volatile uint32_t *)(uintptr_t)0x00000000UL = 0xBADF00DUL;
    }
#elif defined(TEST_SCENARIO_mpu_execute_sram)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO mpu_execute_sram\n");
        ((void (*)(void))(uintptr_t)0x20000000UL)();
    }
#elif defined(TEST_SCENARIO_mpu_write_bootloader)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO mpu_write_bootloader\n");
        *(volatile uint32_t *)(uintptr_t)STM32F429_BOOTLOADER_BASE =
            0xBADF00DUL;
    }
#elif defined(TEST_SCENARIO_mpu_write_metadata)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO mpu_write_metadata\n");
        *(volatile uint32_t *)(uintptr_t)STM32F429_BOOT_METADATA_A_BASE =
            0xBADF00DUL;
    }
#elif defined(TEST_SCENARIO_mpu_stack_guard)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO mpu_stack_guard\n");
        *(volatile uint32_t *)(uintptr_t)mpu_policy_stack_guard_start() =
            0xBADF00DUL;
    }
#elif defined(TEST_SCENARIO_mpu_valid_application)
    if (tick == 10UL) {
        uart_puts("TEST_SCENARIO mpu_valid_application PASS\n");
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
    defined(TEST_SCENARIO_trial_software_reset) || \
    defined(TEST_SCENARIO_mpu_null_access) || \
    defined(TEST_SCENARIO_mpu_execute_sram) || \
    defined(TEST_SCENARIO_mpu_write_bootloader) || \
    defined(TEST_SCENARIO_mpu_write_metadata) || \
    defined(TEST_SCENARIO_mpu_stack_guard)
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
