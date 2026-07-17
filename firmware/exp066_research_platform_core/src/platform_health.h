#ifndef PLATFORM_HEALTH_H
#define PLATFORM_HEALTH_H

#include <stdint.h>

#define PLATFORM_HEALTH_LED_TEST_TICKS 64U
#define PLATFORM_HEALTH_EASTER_EGG_TICKS 96U

#define PLATFORM_RESET_IWDG_FLAG (1UL << 29)
#define PLATFORM_RESET_WWDG_FLAG (1UL << 30)

typedef enum {
    PLATFORM_BOOTING = 0,
    PLATFORM_HEALTHY = 1,
    PLATFORM_DEGRADED = 2,
    PLATFORM_UPDATING = 3,
    PLATFORM_RECOVERY = 4,
    PLATFORM_TEST_RUNNING = 5,
    PLATFORM_FAULT = 6,
    PLATFORM_SECURITY_FAILURE = 7
} platform_health_state_t;

void platform_health_init(void);
void platform_health_set_state(platform_health_state_t state);
void platform_health_report_fault(void);
void platform_health_report_security_failure(void);
void platform_health_acknowledge(void);
void platform_health_service_tick(void);
void platform_health_start_led_test(platform_health_state_t state);
void platform_health_start_easteregg_knightrider(void);
void platform_health_stop_temporary(void);
void platform_health_apply_boot_policy(
    uint32_t reset_csr,
    uint8_t fault_record_valid,
    uint8_t self_tests_passed,
    uint8_t security_failure
);
platform_health_state_t platform_health_evaluate_boot_policy(
    uint32_t reset_csr,
    uint8_t fault_record_valid,
    uint8_t self_tests_passed,
    uint8_t security_failure
);
platform_health_state_t platform_health_get_state(void);
platform_health_state_t platform_health_get_automatic_state(void);
uint8_t platform_health_get_led_mask(void);
uint8_t platform_health_temporary_active(void);
const char *platform_health_state_name(platform_health_state_t state);

#endif
