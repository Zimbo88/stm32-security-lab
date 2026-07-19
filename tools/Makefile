CC ?= cc
BUILD := build
BOOTLOADER := ../firmware/exp045_bootloader_v2
COMMON := ../firmware/common

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-I. -I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON)
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

BOOT_METADATA_PROVISION := $(BUILD)/boot_metadata_provision.bin

.PHONY: all boot-metadata-provision test clean

all: $(BOOT_METADATA_PROVISION)

boot-metadata-provision: $(BOOT_METADATA_PROVISION)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot_metadata_provision.o: boot_metadata_provision.c boot_metadata_provision_core.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_metadata_provision_core.o: boot_metadata_provision_core.c boot_metadata_provision_core.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_metadata.o: $(BOOTLOADER)/src/boot_metadata.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot_flash.o: $(BOOTLOADER)/src/boot_flash.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BOOT_METADATA_PROVISION): \
	$(BUILD)/boot_metadata_provision.o \
	$(BUILD)/boot_metadata_provision_core.o \
	$(BUILD)/boot_metadata.o \
	$(BUILD)/boot_flash.o
	$(CC) $^ $(LDFLAGS) -o $@

test: $(BOOT_METADATA_PROVISION)
	$(BOOT_METADATA_PROVISION) --help >/dev/null

clean:
	rm -rf $(BUILD)
