#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "mpu_policy.h"
#include "stm32f429_memory_layout.h"

static void test_policy_descriptors(void)
{
    mpu_policy_region_t regions[MPU_POLICY_REGION_COUNT];

    assert(mpu_policy_build_regions(regions) != 0U);
    assert(regions[0].base == STM32F429_FLASH_BASE);
    assert(regions[0].size == 0x00100000UL);
    assert(strcmp(regions[0].name, "internal-flash-read-only") == 0);
    assert(regions[1].base == STM32F429_MAIN_SRAM_BASE);
    assert(regions[1].size == 0x00020000UL);
    assert(strcmp(regions[1].name, "main-sram-no-execute") == 0);
    assert(regions[2].base == STM32F429_BOOTLOADER_BASE);
    assert(regions[2].size == 0x00020000UL);
    assert(strcmp(regions[2].name, "stage0-and-metadata-read-only") == 0);
    assert(regions[3].base == 0UL);
    assert(regions[3].size == 32UL);
    assert(regions[4].base == 0x2001E000UL);
    assert(regions[4].size == MPU_POLICY_STACK_GUARD_SIZE);
}

static void test_policy_alignment(void)
{
    mpu_policy_region_t regions[MPU_POLICY_REGION_COUNT];

    assert(mpu_policy_build_regions(regions) != 0U);
    for (uint32_t i = 0U; i < MPU_POLICY_REGION_COUNT; ++i) {
        assert((regions[i].base & (regions[i].size - 1UL)) == 0UL);
        assert((regions[i].attributes & 1UL) != 0UL);
    }
    assert(mpu_policy_stack_guard_start() == 0x2001E000UL);
}

int main(void)
{
    test_policy_descriptors();
    test_policy_alignment();
    return 0;
}
