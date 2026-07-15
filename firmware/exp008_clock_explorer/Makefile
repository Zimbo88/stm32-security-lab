PROJECT := exp008_clock_explorer
BUILD   := build
SRC     := src

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPUFLAGS := -mcpu=cortex-m4 -mthumb

CFLAGS := $(CPUFLAGS) \
          -std=c11 \
          -O0 \
          -g3 \
          -ffreestanding \
          -fdata-sections \
          -ffunction-sections \
          -Wall \
          -Wextra \
          -Werror \
          -I$(SRC)

ASFLAGS := $(CPUFLAGS) \
           -x assembler-with-cpp \
           -g3

LDFLAGS := $(CPUFLAGS) \
           -T linker.ld \
           -nostdlib \
           -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/$(PROJECT).map

OBJECTS := \
    $(BUILD)/startup.o \
    $(BUILD)/main.o \
    $(BUILD)/uart.o \
    $(BUILD)/gpio.o \
    $(BUILD)/delay.o \
    $(BUILD)/clock.o

.PHONY: all clean flash

all: $(BUILD)/$(PROJECT).elf $(BUILD)/$(PROJECT).bin
	$(SIZE) $(BUILD)/$(PROJECT).elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/main.o: $(SRC)/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart.o: $(SRC)/uart.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gpio.o: $(SRC)/gpio.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/delay.o: $(SRC)/delay.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/clock.o: $(SRC)/clock.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(PROJECT).elf: $(OBJECTS) linker.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

flash: all
	st-flash --reset write $(BUILD)/$(PROJECT).bin 0x08000000

clean:
	rm -rf $(BUILD)
