#ifndef MPU_H
#define MPU_H

#include <stdint.h>

#define MPU_TEST_ADDRESS 0x20010000UL

void mpu_configure_test_region(void);
uint32_t mpu_is_enabled(void);

#endif
