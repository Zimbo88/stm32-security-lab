#ifndef PLATFORM_TEST_SCENARIOS_H
#define PLATFORM_TEST_SCENARIOS_H

#include <stdint.h>

void test_scenario_init(void);
void test_scenario_service(uint32_t tick);
uint8_t test_scenario_health_ok(void);
uint8_t test_scenario_stable(uint32_t tick);

#endif
