#include "boot_policy.h"

verify_status_t boot_policy_verify(void)
{
    return signed_image_verify();
}
