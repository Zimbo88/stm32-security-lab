#include "boot_mode.h"

boot_mode_t boot_mode_detect(void)
{
    /*
     * EXP049 establishes the operating-mode interface only.
     * Physical recovery input support will be added separately.
     */
    return BOOT_MODE_NORMAL;
}

const char *boot_mode_text(boot_mode_t mode)
{
    switch (mode) {
    case BOOT_MODE_NORMAL:
        return "NORMAL";

    case BOOT_MODE_RECOVERY:
        return "RECOVERY";

    default:
        return "UNKNOWN";
    }
}
