CC ?= cc
BUILD := build
BOOTLOADER := ../../firmware/exp045_bootloader_v2
COMMON := ../../firmware/common

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-DBOOT_FLASH_TARGET_HOST_TEST -DSIGNED_IMAGE_HOST_TEST \
	-I. -I../update_storage -I../../tools \
	-I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON)
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

ifeq ($(COVERAGE),1)
CFLAGS += --coverage -O0
LDFLAGS += --coverage
endif

SOURCES := \
	test_update_protocol.c \
	../update_storage/simulated_flash.c \
	../host_verifier/performance_stub.c \
	$(BOOTLOADER)/src/byte_reader.c \
	$(BOOTLOADER)/src/boot_flash.c \
	$(BOOTLOADER)/src/boot_metadata.c \
	$(BOOTLOADER)/src/boot_slot.c \
	$(BOOTLOADER)/src/signed_image.c \
	$(BOOTLOADER)/src/update_installer.c \
	$(BOOTLOADER)/src/update_package.c \
	$(BOOTLOADER)/src/update_protocol.c \
	$(BOOTLOADER)/src/update_service.c \
	$(BOOTLOADER)/src/monocypher.c \
	$(BOOTLOADER)/src/monocypher-ed25519.c

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SOURCES)))
TARGET := $(BUILD)/update_protocol_tests

.PHONY: all test clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_update_protocol.o: test_update_protocol.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/simulated_flash.o: ../update_storage/simulated_flash.c ../update_storage/simulated_flash.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/performance_stub.o: ../host_verifier/performance_stub.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/byte_reader.o: $(BOOTLOADER)/src/byte_reader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flash.o: $(BOOTLOADER)/src/boot_flash.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_metadata.o: $(BOOTLOADER)/src/boot_metadata.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_slot.o: $(BOOTLOADER)/src/boot_slot.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/signed_image.o: $(BOOTLOADER)/src/signed_image.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/update_installer.o: $(BOOTLOADER)/src/update_installer.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/update_package.o: $(BOOTLOADER)/src/update_package.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/update_protocol.o: $(BOOTLOADER)/src/update_protocol.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/update_service.o: $(BOOTLOADER)/src/update_service.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/monocypher.o: $(BOOTLOADER)/src/monocypher.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/monocypher-ed25519.o: $(BOOTLOADER)/src/monocypher-ed25519.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

test: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD)
