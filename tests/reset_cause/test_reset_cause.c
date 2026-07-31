#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "reset_cause.h"

#define IWDG (1UL << 29)
#define WWDG (1UL << 30)
#define SOFTWARE (1UL << 28)
#define BOR (1UL << 25)
#define PIN (1UL << 26)
#define POR (1UL << 27)
#define LOW_POWER (1UL << 31)

static void expect(uint32_t raw, reset_cause_kind_t kind, uint32_t result)
{
    assert(reset_cause_primary_kind_from_raw(raw) == kind);
    assert(reset_cause_trial_result(raw) == result);
}

int main(void)
{
    expect(0U, RESET_CAUSE_KIND_NONE, RESET_CAUSE_RESULT_NONE);
    expect(IWDG | SOFTWARE, RESET_CAUSE_KIND_IWDG, RESET_CAUSE_RESULT_IWDG);
    expect(WWDG | SOFTWARE, RESET_CAUSE_KIND_WWDG, RESET_CAUSE_RESULT_WWDG);
    expect(SOFTWARE | BOR, RESET_CAUSE_KIND_SOFTWARE, RESET_CAUSE_RESULT_SOFTWARE);
    expect(BOR | PIN | POR, RESET_CAUSE_KIND_BROWNOUT, RESET_CAUSE_RESULT_BROWNOUT);
    expect(PIN | POR, RESET_CAUSE_KIND_PIN, RESET_CAUSE_RESULT_PIN);
    expect(POR | LOW_POWER, RESET_CAUSE_KIND_POWER_ON, RESET_CAUSE_RESULT_POWER_ON);
    expect(LOW_POWER, RESET_CAUSE_KIND_LOW_POWER, RESET_CAUSE_RESULT_LOW_POWER);
    assert(strcmp(reset_cause_kind_text(RESET_CAUSE_KIND_IWDG), "IWDG") == 0);
    assert(strcmp(reset_cause_kind_text(RESET_CAUSE_KIND_UNKNOWN), "UNKNOWN") == 0);
    puts("reset cause tests passed");
    return 0;
}
