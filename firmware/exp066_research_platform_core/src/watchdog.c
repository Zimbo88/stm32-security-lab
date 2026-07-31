#include "watchdog.h"

#define REG32(address) (*(volatile uint32_t *)(address))

#define IWDG_KR REG32(0x40003000UL)
#define IWDG_PR REG32(0x40003004UL)
#define IWDG_RLR REG32(0x40003008UL)
#define IWDG_SR REG32(0x4000300CUL)
#define RCC_CSR REG32(0x40023874UL)

#define IWDG_KEY_ENABLE 0xCCCCUL
#define IWDG_KEY_ACCESS 0x5555UL
#define IWDG_KEY_REFRESH 0xAAAAUL
#define IWDG_SR_PVU (1UL << 0)
#define IWDG_SR_RVU (1UL << 1)
#define RCC_CSR_LSION (1UL << 0)
#define RCC_CSR_LSIRDY (1UL << 1)
#define IWDG_CONFIGURATION_WAIT 1000000UL

static uint8_t active;

uint8_t platform_watchdog_init(void)
{
    uint32_t wait = 0UL;

    /* Starting IWDG selects the independent LSI clock in hardware. No
       debugger, option byte, or application clock is involved. */
    /* A previous application may have left IWDG running across SYSRESETREQ.
       Refresh once before the reconfiguration window so that the bounded
       setup itself cannot turn a recoverable reset into a loop. */
    IWDG_KR = IWDG_KEY_REFRESH;
    /* Starting before programming also covers a target that arrived with a
       pending PVU/RVU update from an earlier application image. */
    IWDG_KR = IWDG_KEY_ENABLE;
    IWDG_KR = IWDG_KEY_REFRESH;
    IWDG_KR = IWDG_KEY_ACCESS;
    RCC_CSR |= RCC_CSR_LSION;
    wait = 0UL;
    while (((RCC_CSR & RCC_CSR_LSIRDY) == 0UL) &&
           (wait < IWDG_CONFIGURATION_WAIT)) {
        ++wait;
    }
    if ((RCC_CSR & RCC_CSR_LSIRDY) == 0UL) {
        active = 0U;
        return 0U;
    }
    IWDG_PR = PLATFORM_IWDG_PRESCALER_REGISTER;
    IWDG_RLR = PLATFORM_IWDG_RELOAD;
    while (((IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0UL) &&
           (wait < IWDG_CONFIGURATION_WAIT)) {
        ++wait;
    }
    if ((IWDG_SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0UL) {
        active = 0U;
        return 0U;
    }

    IWDG_KR = IWDG_KEY_ENABLE;
    IWDG_KR = IWDG_KEY_REFRESH;
    active = 1U;
    return 1U;
}

void platform_watchdog_refresh(void)
{
    if (active != 0U) {
        IWDG_KR = IWDG_KEY_REFRESH;
    }
}

uint8_t platform_watchdog_active(void)
{
    return active;
}
