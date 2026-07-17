#include "recovery_policy.h"

recovery_policy_status_t recovery_policy_evaluate(boot_mode_t mode)
{
    if (mode == BOOT_MODE_RECOVERY) {
        return RECOVERY_POLICY_UNAVAILABLE;
    }

    return RECOVERY_POLICY_NOT_REQUESTED;
}

const char *recovery_policy_status_text(recovery_policy_status_t status)
{
    switch (status) {
    case RECOVERY_POLICY_NOT_REQUESTED:
        return "NOT REQUESTED";

    case RECOVERY_POLICY_UNAVAILABLE:
        return "UNAVAILABLE";

    default:
        return "UNKNOWN";
    }
}
