CC ?= cc
BUILD := build
APP := ../../firmware/exp066_research_platform_core/src
COMMON := ../../firmware/common
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-DMPU_POLICY_HOST_TEST -I$(APP) -I$(COMMON)

.PHONY: all test clean

all: $(BUILD)/test_mpu_policy

$(BUILD):
	mkdir -p $@

$(BUILD)/test_mpu_policy.o: test_mpu_policy.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mpu_policy.o: $(APP)/mpu_policy.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test_mpu_policy: $(BUILD)/test_mpu_policy.o $(BUILD)/mpu_policy.o
	$(CC) $^ -o $@

test: $(BUILD)/test_mpu_policy
	$(BUILD)/test_mpu_policy

clean:
	rm -rf $(BUILD)
