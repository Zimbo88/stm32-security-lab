#include "mpu_policy.h"

#include "stm32f429_memory_layout.h"

#define MPU_TYPE (*(volatile uint32_t *)(0xE000ED90UL))
#define MPU_CTRL (*(volatile uint32_t *)(0xE000ED94UL))
#define MPU_RNR  (*(volatile uint32_t *)(0xE000ED98UL))
#define MPU_RBAR (*(volatile uint32_t *)(0xE000ED9CUL))
#define MPU_RASR (*(volatile uint32_t *)(0xE000EDA0UL))
#define SCB_SHCSR (*(volatile uint32_t *)(0xE000ED24UL))

#define MPU_CTRL_ENABLE       (1UL << 0)
#define MPU_CTRL_PRIVDEFENA   (1UL << 2)
#define SCB_SHCSR_MEMFAULTENA (1UL << 16)
#define MPU_RASR_ENABLE       (1UL << 0)
#define MPU_RASR_XN           (1UL << 28)
#define MPU_RASR_AP_PRIV_RO   (5UL << 24)
#define MPU_RASR_AP_FULL      (3UL << 24)
#define MPU_RASR_SIZE(size)   ((((uint32_t)(size)) == 32UL) ? (4UL << 1) : \
    (((uint32_t)(size)) == 256UL) ? (7UL << 1) : \
    (((uint32_t)(size)) == 128UL * 1024UL) ? (16UL << 1) : \
    (((uint32_t)(size)) == 1024UL * 1024UL) ? (19UL << 1) : 0UL)

#define MPU_POLICY_FLASH_ATTRIBUTES \
    (MPU_RASR_AP_PRIV_RO | MPU_RASR_SIZE(1024UL * 1024UL) | MPU_RASR_ENABLE)
#define MPU_POLICY_SRAM_ATTRIBUTES \
    (MPU_RASR_XN | MPU_RASR_AP_FULL | MPU_RASR_SIZE(128UL * 1024UL) | \
     MPU_RASR_ENABLE)
#define MPU_POLICY_PROTECTED_FLASH_ATTRIBUTES \
    (MPU_RASR_XN | MPU_RASR_AP_PRIV_RO | MPU_RASR_SIZE(128UL * 1024UL) | \
     MPU_RASR_ENABLE)
#define MPU_POLICY_NO_ACCESS_ATTRIBUTES \
    (MPU_RASR_XN | MPU_RASR_SIZE(32UL) | MPU_RASR_ENABLE)
#define MPU_POLICY_STACK_GUARD_ATTRIBUTES \
    (MPU_RASR_XN | MPU_RASR_SIZE(MPU_POLICY_STACK_GUARD_SIZE) | \
     MPU_RASR_ENABLE)

#if defined(MPU_POLICY_HOST_TEST)
#define MPU_POLICY_STACK_GUARD_BASE 0x2001E000UL
#else
extern uint8_t _stack_guard_start;
#define MPU_POLICY_STACK_GUARD_BASE ((uint32_t)(uintptr_t)&_stack_guard_start)
#endif

#if !defined(MPU_POLICY_HOST_TEST)
static uint32_t saved_mpu_ctrl;
static uint8_t policy_enabled;
#endif

static uint8_t region_valid(uint32_t base, uint32_t size)
{
    if ((size == 0UL) || ((size & (size - 1UL)) != 0UL)) {
        return 0U;
    }
    return ((base & (size - 1UL)) == 0UL) ? 1U : 0U;
}

uint8_t mpu_policy_build_regions(
    mpu_policy_region_t regions[MPU_POLICY_REGION_COUNT]
)
{
    if (regions == 0) {
        return 0U;
    }

    regions[0] = (mpu_policy_region_t){
        .number = 0U,
        .base = STM32F429_FLASH_BASE,
        .size = 1024UL * 1024UL,
        .attributes = MPU_POLICY_FLASH_ATTRIBUTES,
        .name = "internal-flash-read-only"
    };
    regions[1] = (mpu_policy_region_t){
        .number = 1U,
        .base = STM32F429_MAIN_SRAM_BASE,
        .size = 128UL * 1024UL,
        .attributes = MPU_POLICY_SRAM_ATTRIBUTES,
        .name = "main-sram-no-execute"
    };
    /* 128 KiB is the smallest aligned MPU region that contains sectors 0-3.
       This protects Stage 0 and both metadata sectors as one higher-priority
       read-only, non-executable region. */
    regions[2] = (mpu_policy_region_t){
        .number = 2U,
        .base = STM32F429_FLASH_BASE,
        .size = 128UL * 1024UL,
        .attributes = MPU_POLICY_PROTECTED_FLASH_ATTRIBUTES,
        .name = "stage0-and-metadata-read-only"
    };
    regions[3] = (mpu_policy_region_t){
        .number = 3U,
        .base = 0x00000000UL,
        .size = 32UL,
        .attributes = MPU_POLICY_NO_ACCESS_ATTRIBUTES,
        .name = "null-no-access"
    };
    regions[4] = (mpu_policy_region_t){
        .number = 4U,
        .base = MPU_POLICY_STACK_GUARD_BASE,
        .size = MPU_POLICY_STACK_GUARD_SIZE,
        .attributes = MPU_POLICY_STACK_GUARD_ATTRIBUTES,
        .name = "descending-stack-guard"
    };

    for (uint32_t index = 0U; index < MPU_POLICY_REGION_COUNT; ++index) {
        if (region_valid(regions[index].base, regions[index].size) == 0U) {
            return 0U;
        }
    }
    return 1U;
}

#if !defined(MPU_POLICY_HOST_TEST)
uint8_t mpu_policy_init(void)
{
    mpu_policy_region_t regions[MPU_POLICY_REGION_COUNT];

    if ((MPU_TYPE & 0x0000FF00UL) <
        (MPU_POLICY_REGION_COUNT << 8U) ||
        mpu_policy_build_regions(regions) == 0U) {
        policy_enabled = 0U;
        return 0U;
    }

    MPU_CTRL = 0UL;
    __asm volatile ("dsb" ::: "memory");
    for (uint32_t index = 0U; index < MPU_POLICY_REGION_COUNT; ++index) {
        MPU_RNR = regions[index].number;
        MPU_RBAR = regions[index].base;
        MPU_RASR = regions[index].attributes;
    }
    SCB_SHCSR |= SCB_SHCSR_MEMFAULTENA;
    MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;
    __asm volatile ("dsb\n isb" ::: "memory");
    policy_enabled = 1U;
    return 1U;
}

uint8_t mpu_policy_is_enabled(void)
{
    return policy_enabled;
}

void mpu_policy_suspend_for_metadata_commit(void)
{
    saved_mpu_ctrl = MPU_CTRL;
    MPU_CTRL = 0UL;
    __asm volatile ("dsb\n isb" ::: "memory");
}

void mpu_policy_resume_after_metadata_commit(void)
{
    MPU_CTRL = saved_mpu_ctrl;
    __asm volatile ("dsb\n isb" ::: "memory");
}
#else
uint8_t mpu_policy_init(void) { return 0U; }
uint8_t mpu_policy_is_enabled(void) { return 0U; }
void mpu_policy_suspend_for_metadata_commit(void) { }
void mpu_policy_resume_after_metadata_commit(void) { }
#endif

uint32_t mpu_policy_stack_guard_start(void)
{
    return MPU_POLICY_STACK_GUARD_BASE;
}
