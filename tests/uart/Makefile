CC ?= cc
BUILD := build
BOOTLOADER := ../../firmware/exp045_bootloader_v2
COMMON := ../../firmware/common

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic \
	-Wshadow -Wpointer-arith -Wcast-align \
	-DUART_HOST_TEST \
	-I$(BOOTLOADER)/src -I$(BOOTLOADER)/include -I$(COMMON)
LDFLAGS :=

ifeq ($(SANITIZE),1)
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address,undefined
endif

SOURCES := \
	test_uart.c \
	$(BOOTLOADER)/src/byte_reader.c \
	$(BOOTLOADER)/src/board_clock.c \
	$(BOOTLOADER)/src/uart.c

OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SOURCES)))
TARGET := $(BUILD)/uart_tests

.PHONY: all test clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_uart.o: test_uart.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/byte_reader.o: $(BOOTLOADER)/src/byte_reader.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/board_clock.o: $(BOOTLOADER)/src/board_clock.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart.o: $(BOOTLOADER)/src/uart.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

test: $(TARGET)
	$(TARGET)

clean:
	rm -rf $(BUILD)
