PROJECT := exp002_gpio_led
BUILD   := build
SRC     := src

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS   := $(CPUFLAGS) -std=c11 -O0 -g3 \
            -ffreestanding -fdata-sections -ffunction-sections \
            -Wall -Wextra -Werror
ASFLAGS  := $(CPUFLAGS) -x assembler-with-cpp -g3
LDFLAGS  := $(CPUFLAGS) -T linker.ld -nostdlib \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(PROJECT).map

OBJECTS := \
	$(BUILD)/startup.o \
	$(BUILD)/main.o

.PHONY: all clean flash readback

all: $(BUILD)/$(PROJECT).elf $(BUILD)/$(PROJECT).bin
	$(SIZE) $(BUILD)/$(PROJECT).elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/main.o: $(SRC)/main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(PROJECT).elf: $(OBJECTS) linker.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

flash: all
	st-flash --reset write $(BUILD)/$(PROJECT).bin 0x08000000

readback:
	st-flash read $(BUILD)/readback.bin 0x08000000 4096
	sha256sum $(BUILD)/readback.bin

clean:
	rm -rf $(BUILD)
