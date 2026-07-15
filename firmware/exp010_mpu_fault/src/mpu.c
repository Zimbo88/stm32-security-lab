#include "mpu.h"

#define REG32(address) (*(volatile uint32_t *)(address))

#define MPU_TYPE  REG32(0xE000ED90UL)
#define MPU_CTRL  REG32(0xE000ED94UL)
#define MPU_RNR   REG32(0xE000ED98UL)
#define MPU_RBAR  REG32(0xE000ED9CUL)
#define MPU_RASR  REG32(0xE000EDA0UL)

#define SCB_SHCSR REG32(0xE000ED24UL)

#define MPU_CTRL_ENABLE       (1UL << 0)
#define MPU_CTRL_PRIVDEFENA   (1UL << 2)

#define SCB_SHCSR_MEMFAULTENA (1UL << 16)

#define MPU_RASR_ENABLE       (1UL << 0)
#define MPU_RASR_SIZE_32B     (4UL << 1)
#define MPU_RASR_XN           (1UL << 28)

void mpu_configure_test_region(void)
{
    /* MemManage-Fault separat aktivieren. */
    SCB_SHCSR |= SCB_SHCSR_MEMFAULTENA;

    /* MPU während der Konfiguration ausschalten. */
    MPU_CTRL = 0U;

    /*
     * Region 7:
     * 0x20010000 bis 0x2001001F
     * 32 Byte, keine Zugriffsrechte, nicht ausführbar.
     */
    MPU_RNR = 7U;
    MPU_RBAR = MPU_TEST_ADDRESS;
    MPU_RASR =
        MPU_RASR_XN |
        MPU_RASR_SIZE_32B |
        MPU_RASR_ENABLE;

    /*
     * MPU aktivieren.
     * PRIVDEFENA erhält für privilegierten Code die normale Memory Map
     * außerhalb unserer expliziten Testregion.
     */
    MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;

    __asm volatile (
        "dsb\n"
        "isb\n"
        ::: "memory"
    );
}

uint32_t mpu_is_enabled(void)
{
    (void)MPU_TYPE;
    return (MPU_CTRL & MPU_CTRL_ENABLE) != 0U ? 1U : 0U;
}
