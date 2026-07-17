#ifndef RECOVERY_POLICY_H
#define RECOVERY_POLICY_H

#include "boot_mode.h"

typedef enum {
    RECOVERY_POLICY_NOT_REQUESTED = 0,
    RECOVERY_POLICY_UNAVAILABLE
} recovery_policy_status_t;

recovery_policy_status_t recovery_policy_evaluate(boot_mode_t mode);
const char *recovery_policy_status_text(recovery_policy_status_t status);

#endif
