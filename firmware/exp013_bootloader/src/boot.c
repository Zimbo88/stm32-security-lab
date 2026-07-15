#include "boot.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define SCB_VTOR   REG32(0xE000ED08UL)
#define SYST_CSR   REG32(0xE000E010UL)
#define NVIC_ICER0 REG32(0xE000E180UL)
#define NVIC_ICPR0 REG32(0xE000E280UL)

typedef void (*entry_fn_t)(void);

static uint32_t app_initial_msp(void)
{
    return *(volatile const uint32_t *)(APP_BASE + 0U);
}

static uint32_t app_reset_vector(void)
{
    return *(volatile const uint32_t *)(APP_BASE + 4U);
}

uint32_t boot_application_is_valid(void)
{
    const uint32_t msp = app_initial_msp();
    const uint32_t reset = app_reset_vector();

    const uint32_t msp_valid =
        (msp >= 0x20000000UL) && (msp <= 0x20020000UL);

    const uint32_t reset_in_flash =
        ((reset & ~1UL) >= APP_BASE) &&
        ((reset & ~1UL) < 0x08100000UL);

    const uint32_t thumb_valid = (reset & 1UL) != 0U;

    return (msp_valid && reset_in_flash && thumb_valid) ? 1U : 0U;
}

void boot_jump_to_application(void)
{
    const uint32_t new_msp = app_initial_msp();
    const uint32_t reset = app_reset_vector();
    const entry_fn_t app_entry = (entry_fn_t)reset;

    __asm volatile ("cpsid i" ::: "memory");

    SYST_CSR = 0U;

    for (uint32_t i = 0U; i < 8U; ++i) {
        NVIC_ICER0 = 0xFFFFFFFFUL;
        NVIC_ICPR0 = 0xFFFFFFFFUL;
    }

    SCB_VTOR = APP_BASE;

    __asm volatile (
        "dsb\n"
        "isb\n"
        "msr msp, %0\n"
        :
        : "r" (new_msp)
        : "memory"
    );

    app_entry();

    for (;;) {
    }
}
