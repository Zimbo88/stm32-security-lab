CC ?= cc
BUILD := build
BOOTLOADER := ../../firmware/exp045_bootloader_v2
COMMON := ../../firmware/common

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-DBOOT_FLASH_TARGET_HOST_TEST \
	-I../../tools -I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON)
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SOURCES := \
	test_update_storage.c \
	simulated_flash.c \
	../../tools/boot_metadata_provision_core.c \
	../host_verifier/performance_stub.c \
	$(BOOTLOADER)/src/boot_flash.c \
	$(BOOTLOADER)/src/boot_flash_target.c \
	$(BOOTLOADER)/src/boot_metadata.c \
	$(BOOTLOADER)/src/boot_confirmation.c \
	$(BOOTLOADER)/src/boot_slot.c \
	$(BOOTLOADER)/src/boot_slot_selection.c \
	$(BOOTLOADER)/src/signed_image.c \
	$(BOOTLOADER)/src/update_installer.c \
	$(BOOTLOADER)/src/update_package.c \
	$(BOOTLOADER)/src/monocypher.c \
	$(BOOTLOADER)/src/monocypher-ed25519.c

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

$(BUILD)/boot_metadata_provision_core.o: ../../tools/boot_metadata_provision_core.c ../../tools/boot_metadata_provision_core.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/performance_stub.o: ../host_verifier/performance_stub.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flash.o: $(BOOTLOADER)/src/boot_flash.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flash_target.o: $(BOOTLOADER)/src/boot_flash_target.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_metadata.o: $(BOOTLOADER)/src/boot_metadata.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_confirmation.o: $(BOOTLOADER)/src/boot_confirmation.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_slot.o: $(BOOTLOADER)/src/boot_slot.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_slot_selection.o: $(BOOTLOADER)/src/boot_slot_selection.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/signed_image.o: $(BOOTLOADER)/src/signed_image.c | $(BUILD)
	$(CC) $(CFLAGS) -DSIGNED_IMAGE_HOST_TEST -c $< -o $@

$(BUILD)/update_installer.o: $(BOOTLOADER)/src/update_installer.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/update_package.o: $(BOOTLOADER)/src/update_package.c | $(BUILD)
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
