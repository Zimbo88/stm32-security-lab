#ifndef BOOT_MODE_H
#define BOOT_MODE_H

typedef enum {
    BOOT_MODE_NORMAL = 0,
    BOOT_MODE_RECOVERY
} boot_mode_t;

boot_mode_t boot_mode_detect(void);
const char *boot_mode_text(boot_mode_t mode);

#endif
