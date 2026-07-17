#ifndef BOOT_RESULT_H
#define BOOT_RESULT_H

#include "signed_image.h"

void boot_result_print(verify_status_t status);
_Noreturn void boot_result_halt(void);

#endif
