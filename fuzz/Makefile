CC ?= cc
BUILD := build
BOOTLOADER := ../firmware/exp045_bootloader_v2
COMMON := ../firmware/common

BASE_CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-fno-omit-frame-pointer -DBOOT_FLASH_TARGET_HOST_TEST \
	-DSIGNED_IMAGE_HOST_TEST \
	-I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON) \
	-I../tests/update_storage

SANITIZERS ?= address,undefined
CFLAGS := $(BASE_CFLAGS) -fsanitize=$(SANITIZERS)
LDFLAGS := -fsanitize=$(SANITIZERS)

SOURCES := \
	harnesses/host_fuzz_driver.c \
	../tests/host_verifier/performance_stub.c \
	$(BOOTLOADER)/src/byte_reader.c \
	$(BOOTLOADER)/src/boot_flash.c \
	$(BOOTLOADER)/src/boot_metadata.c \
	$(BOOTLOADER)/src/boot_slot.c \
	$(BOOTLOADER)/src/boot_slot_selection.c \
	$(BOOTLOADER)/src/signed_image.c \
	$(BOOTLOADER)/src/update_installer.c \
	$(BOOTLOADER)/src/update_package.c \
	$(BOOTLOADER)/src/update_protocol.c \
	$(BOOTLOADER)/src/update_service.c \
	$(BOOTLOADER)/src/monocypher.c \
	$(BOOTLOADER)/src/monocypher-ed25519.c

TARGET := $(BUILD)/host_fuzz_driver
LIBFUZZ_TARGET := $(BUILD)/libfuzzer_parser
LIBFUZZ_SOURCES := $(filter-out harnesses/host_fuzz_driver.c,$(SOURCES)) \
	harnesses/libfuzzer_entry.c

.PHONY: all test sanitize clean libfuzzer

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

test: $(TARGET)
	$(TARGET) --mode uart --iterations 1000
	$(TARGET) --mode metadata --iterations 1000
	$(TARGET) --mode package --iterations 1000
	$(TARGET) --mode policy --iterations 100

sanitize: $(TARGET)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	$(MAKE) --no-print-directory test

libfuzzer:
	@command -v clang >/dev/null || \
		{ echo "libFuzzer unavailable: clang is not installed"; exit 2; }
	clang $(BASE_CFLAGS) -fsanitize=fuzzer,address,undefined \
		$(LIBFUZZ_SOURCES) -fsanitize=fuzzer,address,undefined \
		-o $(LIBFUZZ_TARGET)

clean:
	rm -rf $(BUILD)
