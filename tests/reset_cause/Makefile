CC ?= cc
BUILD := build
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS += -I../../firmware/exp045_bootloader_v2/src
TARGET := $(BUILD)/reset_cause_tests

.PHONY: all test clean

all: $(TARGET)

$(BUILD):
	mkdir -p $@

$(BUILD)/test_reset_cause.o: test_reset_cause.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/reset_cause.o: ../../firmware/exp045_bootloader_v2/src/reset_cause.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart_stub.o: uart_stub.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(BUILD)/test_reset_cause.o $(BUILD)/reset_cause.o $(BUILD)/uart_stub.o
	$(CC) $^ -o $@

test: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD)
