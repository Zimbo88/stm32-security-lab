CC ?= cc
BUILD := build
BOOTLOADER := ../../firmware/exp045_bootloader_v2
COMMON := ../../firmware/common

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON)
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SOURCES := \
	test_update_storage.c \
	simulated_flash.c \
	$(BOOTLOADER)/src/boot_flash.c \
	$(BOOTLOADER)/src/boot_flash_target.c \
	$(BOOTLOADER)/src/boot_metadata.c \
	$(BOOTLOADER)/src/boot_slot.c

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SOURCES)))
TARGET := $(BUILD)/update_storage_tests

.PHONY: all test clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_update_storage.o: test_update_storage.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/simulated_flash.o: simulated_flash.c simulated_flash.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flash.o: $(BOOTLOADER)/src/boot_flash.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flash_target.o: $(BOOTLOADER)/src/boot_flash_target.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_metadata.o: $(BOOTLOADER)/src/boot_metadata.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_slot.o: $(BOOTLOADER)/src/boot_slot.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

test: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD)
