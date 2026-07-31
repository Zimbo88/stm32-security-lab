#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include "boot_slot_selection.h"
#include "signed_image.h"

verify_status_t boot_policy_verify(void);
boot_slot_selection_status_t boot_policy_select(
    uint32_t reset_csr,
    boot_slot_selection_result_t *result
);

#endif
