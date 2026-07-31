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

typedef enum {
    RESET_CAUSE_KIND_NONE = 0,
    RESET_CAUSE_KIND_IWDG,
    RESET_CAUSE_KIND_WWDG,
    RESET_CAUSE_KIND_SOFTWARE,
    RESET_CAUSE_KIND_BROWNOUT,
    RESET_CAUSE_KIND_PIN,
    RESET_CAUSE_KIND_POWER_ON,
    RESET_CAUSE_KIND_LOW_POWER,
    RESET_CAUSE_KIND_UNKNOWN
} reset_cause_kind_t;

/* Persisted in boot metadata.result; values are intentionally nonzero. */
#define RESET_CAUSE_RESULT_NONE       0UL
#define RESET_CAUSE_RESULT_IWDG       0x1001UL
#define RESET_CAUSE_RESULT_WWDG       0x1002UL
#define RESET_CAUSE_RESULT_SOFTWARE   0x1003UL
#define RESET_CAUSE_RESULT_BROWNOUT   0x1004UL
#define RESET_CAUSE_RESULT_PIN        0x1005UL
#define RESET_CAUSE_RESULT_POWER_ON   0x1006UL
#define RESET_CAUSE_RESULT_LOW_POWER 0x1007UL
#define RESET_CAUSE_RESULT_UNKNOWN    0x10FFUL

reset_cause_t reset_cause_capture(void);
void reset_cause_clear(void);
reset_cause_kind_t reset_cause_primary_kind(const reset_cause_t *cause);
reset_cause_kind_t reset_cause_primary_kind_from_raw(uint32_t raw_csr);
const char *reset_cause_kind_text(reset_cause_kind_t kind);
uint32_t reset_cause_trial_result(uint32_t raw_csr);
void reset_cause_print(const reset_cause_t *cause);

#endif
