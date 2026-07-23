CC ?= cc
BUILD := build
BOOTLOADER := ../../firmware/exp045_bootloader_v2
COMMON := ../../firmware/common

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-I. -I../update_storage \
	-I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON)
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SOURCES := \
	test_diagnostic_console.c \
	../update_storage/simulated_flash.c \
	../host_verifier/performance_stub.c \
	$(BOOTLOADER)/src/boot_flash.c \
	$(BOOTLOADER)/src/boot_metadata.c \
	$(BOOTLOADER)/src/boot_slot.c \
	$(BOOTLOADER)/src/byte_reader.c \
	$(BOOTLOADER)/src/board_clock.c \
	$(BOOTLOADER)/src/diagnostic_console.c

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SOURCES)))
TARGET := $(BUILD)/diagnostic_console_tests

.PHONY: all test clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_diagnostic_console.o: test_diagnostic_console.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/simulated_flash.o: ../update_storage/simulated_flash.c ../update_storage/simulated_flash.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/performance_stub.o: ../host_verifier/performance_stub.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flash.o: $(BOOTLOADER)/src/boot_flash.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_metadata.o: $(BOOTLOADER)/src/boot_metadata.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_slot.o: $(BOOTLOADER)/src/boot_slot.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/byte_reader.o: $(BOOTLOADER)/src/byte_reader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/board_clock.o: $(BOOTLOADER)/src/board_clock.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/diagnostic_console.o: $(BOOTLOADER)/src/diagnostic_console.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

test: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD)
