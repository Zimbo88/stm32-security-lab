#ifndef PLATFORM_MPU_POLICY_H
#define PLATFORM_MPU_POLICY_H

#include <stdint.h>

#define MPU_POLICY_REGION_COUNT 5U
#define MPU_POLICY_STACK_GUARD_SIZE 0x100UL
#define MPU_POLICY_STACK_RESERVE_SIZE 0x2000UL

typedef struct {
    uint32_t number;
    uint32_t base;
    uint32_t size;
    uint32_t attributes;
    const char *name;
} mpu_policy_region_t;

/* Pure descriptor generation is available to host tests. */
uint8_t mpu_policy_build_regions(
    mpu_policy_region_t regions[MPU_POLICY_REGION_COUNT]
);

/* Apply the target MPU policy and enable MemManage faults. */
uint8_t mpu_policy_init(void);
uint8_t mpu_policy_is_enabled(void);

/* The existing confirmation service is the only sanctioned metadata writer. */
void mpu_policy_suspend_for_metadata_commit(void);
void mpu_policy_resume_after_metadata_commit(void);

uint32_t mpu_policy_stack_guard_start(void);

#endif
