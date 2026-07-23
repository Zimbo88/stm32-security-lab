#include "system_reset.h"

#define SCB_AIRCR_ADDRESS       0xE000ED0CUL
#define SCB_AIRCR_VECTKEY       (0x5FAUL << 16)
#define SCB_AIRCR_PRIGROUP_MASK (7UL << 8)
#define SCB_AIRCR_SYSRESETREQ   (1UL << 2)

#ifdef SYSTEM_RESET_HOST_TEST
static uint32_t host_aircr;
static uint32_t host_requested;
#endif

#ifndef SYSTEM_RESET_HOST_TEST
static void reset_barrier(void)
{
    __asm volatile ("dsb" ::: "memory");
}
#endif

void system_reset_request(void)
{
#ifdef SYSTEM_RESET_HOST_TEST
    host_requested += 1UL;
    host_aircr = SCB_AIRCR_VECTKEY | SCB_AIRCR_SYSRESETREQ;
#else
    volatile uint32_t *const aircr =
        (volatile uint32_t *)(uintptr_t)SCB_AIRCR_ADDRESS;
    const uint32_t priority_group = *aircr & SCB_AIRCR_PRIGROUP_MASK;

    reset_barrier();
    *aircr = SCB_AIRCR_VECTKEY | priority_group | SCB_AIRCR_SYSRESETREQ;
    reset_barrier();

    for (;;) {
        __asm volatile ("nop");
    }
#endif
}

#ifdef SYSTEM_RESET_HOST_TEST
void system_reset_host_reset(void)
{
    host_aircr = 0U;
    host_requested = 0U;
}

uint32_t system_reset_host_requested(void)
{
    return host_requested;
}

uint32_t system_reset_host_aircr(void)
{
    return host_aircr;
}
#endif
