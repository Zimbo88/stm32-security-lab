#include "platform_health.h"

#include "platform_led.h"

typedef struct {
    uint8_t mask;
    uint8_t ticks;
} health_frame_t;

static const health_frame_t pattern_booting[] = {
    { PLATFORM_LED1, 6U },
    { PLATFORM_LED1 | PLATFORM_LED2, 6U },
    { PLATFORM_LED1 | PLATFORM_LED2 | PLATFORM_LED3, 6U },
    { PLATFORM_LED_ALL, 6U },
    { 0U, 6U }
};

static const health_frame_t pattern_healthy[] = {
    { PLATFORM_LED4, 1U },
    { 0U, 9U }
};

static const health_frame_t pattern_degraded[] = {
    { PLATFORM_LED_ALL, 2U },
    { 0U, 2U },
    { PLATFORM_LED_ALL, 2U },
    { 0U, 10U }
};

static const health_frame_t pattern_updating[] = {
    { PLATFORM_LED1, 2U },
    { PLATFORM_LED1 | PLATFORM_LED2, 2U },
    { PLATFORM_LED1 | PLATFORM_LED2 | PLATFORM_LED3, 2U },
    { PLATFORM_LED_ALL, 2U },
    { PLATFORM_LED2 | PLATFORM_LED3 | PLATFORM_LED4, 2U },
    { PLATFORM_LED3 | PLATFORM_LED4, 2U },
    { PLATFORM_LED4, 2U },
    { 0U, 2U }
};

static const health_frame_t pattern_recovery[] = {
    { PLATFORM_LED1 | PLATFORM_LED4, 4U },
    { PLATFORM_LED2 | PLATFORM_LED3, 4U }
};

static const health_frame_t pattern_test_running[] = {
    { PLATFORM_LED_ALL, 1U },
    { 0U, 3U }
};

static const health_frame_t pattern_fault[] = {
    { PLATFORM_LED_ALL, 1U },
    { 0U, 1U },
    { PLATFORM_LED_ALL, 1U },
    { 0U, 1U },
    { PLATFORM_LED_ALL, 1U },
    { 0U, 3U }
};

static const health_frame_t pattern_security_failure[] = {
    { PLATFORM_LED_ALL, 1U },
    { 0U, 1U },
    { PLATFORM_LED1 | PLATFORM_LED4, 1U },
    { 0U, 1U },
    { PLATFORM_LED2 | PLATFORM_LED3, 1U },
    { 0U, 1U },
    { PLATFORM_LED_ALL, 2U },
    { 0U, 2U }
};

static platform_health_state_t automatic_state;
static platform_health_state_t active_state;
static platform_health_state_t temporary_state;
static uint32_t temporary_ticks_remaining;
static uint32_t active_phase;
static uint8_t temporary_active_flag;
static uint8_t temporary_fast_flag;
static uint8_t last_led_mask;

static uint8_t state_priority(platform_health_state_t state)
{
    switch (state) {
    case PLATFORM_SECURITY_FAILURE:
        return 8U;
    case PLATFORM_FAULT:
        return 7U;
    case PLATFORM_RECOVERY:
        return 6U;
    case PLATFORM_UPDATING:
        return 5U;
    case PLATFORM_DEGRADED:
        return 4U;
    case PLATFORM_TEST_RUNNING:
        return 3U;
    case PLATFORM_BOOTING:
        return 2U;
    case PLATFORM_HEALTHY:
    default:
        return 1U;
    }
}

static const health_frame_t *frames_for_state(
    platform_health_state_t state,
    uint32_t *count
)
{
    switch (state) {
    case PLATFORM_BOOTING:
        *count = (uint32_t)(sizeof(pattern_booting) / sizeof(pattern_booting[0]));
        return pattern_booting;
    case PLATFORM_DEGRADED:
        *count = (uint32_t)(sizeof(pattern_degraded) / sizeof(pattern_degraded[0]));
        return pattern_degraded;
    case PLATFORM_UPDATING:
        *count = (uint32_t)(sizeof(pattern_updating) / sizeof(pattern_updating[0]));
        return pattern_updating;
    case PLATFORM_RECOVERY:
        *count = (uint32_t)(sizeof(pattern_recovery) / sizeof(pattern_recovery[0]));
        return pattern_recovery;
    case PLATFORM_TEST_RUNNING:
        *count = (uint32_t)(sizeof(pattern_test_running) / sizeof(pattern_test_running[0]));
        return pattern_test_running;
    case PLATFORM_FAULT:
        *count = (uint32_t)(sizeof(pattern_fault) / sizeof(pattern_fault[0]));
        return pattern_fault;
    case PLATFORM_SECURITY_FAILURE:
        *count = (uint32_t)(sizeof(pattern_security_failure) / sizeof(pattern_security_failure[0]));
        return pattern_security_failure;
    case PLATFORM_HEALTHY:
    default:
        *count = (uint32_t)(sizeof(pattern_healthy) / sizeof(pattern_healthy[0]));
        return pattern_healthy;
    }
}

static uint32_t pattern_period(
    const health_frame_t *frames,
    uint32_t count,
    uint8_t fast
)
{
    uint32_t period = 0U;

    for (uint32_t index = 0U; index < count; ++index) {
        period += fast != 0U ? 1U : frames[index].ticks;
    }
    return period == 0U ? 1U : period;
}

static uint8_t pattern_mask(platform_health_state_t state, uint32_t phase, uint8_t fast)
{
    uint32_t count;
    const health_frame_t *frames = frames_for_state(state, &count);
    uint32_t position = phase % pattern_period(frames, count, fast);

    for (uint32_t index = 0U; index < count; ++index) {
        const uint32_t duration = fast != 0U ? 1U : frames[index].ticks;
        if (position < duration) {
            return frames[index].mask;
        }
        position -= duration;
    }
    return 0U;
}

static platform_health_state_t selected_state(void)
{
    if (
        temporary_active_flag != 0U &&
        state_priority(temporary_state) >= state_priority(automatic_state)
    ) {
        return temporary_state;
    }
    return automatic_state;
}

void platform_health_init(void)
{
    platform_led_init();
    automatic_state = PLATFORM_BOOTING;
    active_state = PLATFORM_BOOTING;
    temporary_state = PLATFORM_HEALTHY;
    temporary_ticks_remaining = 0U;
    active_phase = 0U;
    temporary_active_flag = 0U;
    temporary_fast_flag = 0U;
    last_led_mask = 0U;
    platform_led_write(last_led_mask);
}

void platform_health_set_state(platform_health_state_t state)
{
    if (state_priority(state) >= state_priority(automatic_state)) {
        automatic_state = state;
    }
}

void platform_health_report_fault(void)
{
    automatic_state = PLATFORM_FAULT;
}

void platform_health_report_security_failure(void)
{
    automatic_state = PLATFORM_SECURITY_FAILURE;
}

void platform_health_acknowledge(void)
{
    if (automatic_state == PLATFORM_DEGRADED) {
        automatic_state = PLATFORM_HEALTHY;
    }
    platform_health_stop_temporary();
}

void platform_health_start_led_test(platform_health_state_t state)
{
    temporary_state = state;
    temporary_ticks_remaining = PLATFORM_HEALTH_LED_TEST_TICKS;
    temporary_active_flag = 1U;
    temporary_fast_flag = 0U;
    active_phase = 0U;
}

void platform_health_start_easteregg_knightrider(void)
{
    temporary_state = PLATFORM_HEALTHY;
    temporary_ticks_remaining = PLATFORM_HEALTH_EASTER_EGG_TICKS;
    temporary_active_flag = 1U;
    temporary_fast_flag = 1U;
    active_phase = 0U;
}

void platform_health_stop_temporary(void)
{
    temporary_ticks_remaining = 0U;
    temporary_active_flag = 0U;
    temporary_fast_flag = 0U;
    active_phase = 0U;
}

void platform_health_service_tick(void)
{
    platform_health_state_t state = selected_state();
    uint8_t fast = (
        temporary_active_flag != 0U &&
        state == temporary_state &&
        temporary_fast_flag != 0U
    ) ? 1U : 0U;

    if (state != active_state) {
        active_state = state;
        active_phase = 0U;
    }

    last_led_mask = pattern_mask(state, active_phase, fast);
    platform_led_write(last_led_mask);
    ++active_phase;

    if (temporary_active_flag != 0U && temporary_ticks_remaining != 0U) {
        --temporary_ticks_remaining;
        if (temporary_ticks_remaining == 0U) {
            temporary_active_flag = 0U;
            temporary_fast_flag = 0U;
            active_phase = 0U;
        }
    }
}

platform_health_state_t platform_health_evaluate_boot_policy(
    uint32_t reset_csr,
    uint8_t fault_record_valid,
    uint8_t self_tests_passed,
    uint8_t security_failure
)
{
    if (security_failure != 0U) {
        return PLATFORM_SECURITY_FAILURE;
    }
    if (self_tests_passed == 0U) {
        return PLATFORM_FAULT;
    }
    if (
        fault_record_valid != 0U ||
        (reset_csr & (PLATFORM_RESET_IWDG_FLAG | PLATFORM_RESET_WWDG_FLAG)) != 0U
    ) {
        return PLATFORM_DEGRADED;
    }
    return PLATFORM_HEALTHY;
}

void platform_health_apply_boot_policy(
    uint32_t reset_csr,
    uint8_t fault_record_valid,
    uint8_t self_tests_passed,
    uint8_t security_failure
)
{
    const platform_health_state_t state = platform_health_evaluate_boot_policy(
        reset_csr,
        fault_record_valid,
        self_tests_passed,
        security_failure
    );

    if (state == PLATFORM_FAULT) {
        platform_health_report_fault();
    } else if (state == PLATFORM_SECURITY_FAILURE) {
        platform_health_report_security_failure();
    } else {
        automatic_state = state;
    }
}

platform_health_state_t platform_health_get_state(void)
{
    return selected_state();
}

platform_health_state_t platform_health_get_automatic_state(void)
{
    return automatic_state;
}

uint8_t platform_health_get_led_mask(void)
{
    return last_led_mask;
}

uint8_t platform_health_temporary_active(void)
{
    return temporary_active_flag;
}

const char *platform_health_state_name(platform_health_state_t state)
{
    switch (state) {
    case PLATFORM_BOOTING:
        return "BOOTING";
    case PLATFORM_HEALTHY:
        return "HEALTHY";
    case PLATFORM_DEGRADED:
        return "DEGRADED";
    case PLATFORM_UPDATING:
        return "UPDATING";
    case PLATFORM_RECOVERY:
        return "RECOVERY";
    case PLATFORM_TEST_RUNNING:
        return "TEST_RUNNING";
    case PLATFORM_FAULT:
        return "FAULT";
    case PLATFORM_SECURITY_FAILURE:
        return "SECURITY_FAILURE";
    default:
        return "UNKNOWN";
    }
}
