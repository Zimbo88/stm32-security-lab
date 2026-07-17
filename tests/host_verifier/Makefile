CC ?= cc
BUILD := build
BOOTLOADER := ../../firmware/exp045_bootloader_v2
COMMON := ../../firmware/common

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-DSIGNED_IMAGE_HOST_TEST \
	-I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON)
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SOURCES := \
	test_signed_image_verifier.c \
	performance_stub.c \
	$(BOOTLOADER)/src/signed_image.c \
	$(BOOTLOADER)/src/monocypher.c \
	$(BOOTLOADER)/src/monocypher-ed25519.c

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SOURCES)))
TARGET := $(BUILD)/host_signed_image_verifier

.PHONY: all test clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_signed_image_verifier.o: test_signed_image_verifier.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/performance_stub.o: performance_stub.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/signed_image.o: $(BOOTLOADER)/src/signed_image.c | $(BUILD)
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
