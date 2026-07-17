#ifndef RESET_CAUSE_H
#define RESET_CAUSE_H

#include <stdint.h>

typedef struct {
    uint32_t raw_csr;
    uint8_t low_power_reset;
    uint8_t window_watchdog_reset;
    uint8_t independent_watchdog_reset;
    uint8_t software_reset;
    uint8_t power_on_reset;
    uint8_t pin_reset;
    uint8_t brownout_reset;
} reset_cause_t;

reset_cause_t reset_cause_capture(void);
void reset_cause_clear(void);
void reset_cause_print(const reset_cause_t *cause);

#endif
