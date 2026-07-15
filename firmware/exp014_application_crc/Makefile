PROJECT := exp014_application_crc
BUILD   := build
SRC     := src

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS   := $(CPUFLAGS) -std=c11 -O0 -g3 -ffreestanding \
            -fdata-sections -ffunction-sections -Wall -Wextra -Werror -I$(SRC)
ASFLAGS  := $(CPUFLAGS) -x assembler-with-cpp -g3
LDFLAGS  := $(CPUFLAGS) -T linker.ld -nostdlib \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(PROJECT).map

SOURCES_C := $(wildcard $(SRC)/*.c)
OBJECTS_C := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES_C))
OBJECTS   := $(BUILD)/startup.o $(OBJECTS_C)

.PHONY: all clean flash

all: $(BUILD)/$(PROJECT).elf $(BUILD)/exp014_application_crc_image.bin
	$(SIZE) $(BUILD)/$(PROJECT).elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(PROJECT).elf: $(OBJECTS) linker.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/exp014_application_crc_payload.bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/exp014_application_crc_image.bin: $(BUILD)/exp014_application_crc_payload.bin
	python3 ../tools/pack_image.py $< $@ --image-version 1

flash: all
	st-flash --reset write $(BUILD)/exp014_application_crc_image.bin 0x08008000

clean:
	rm -rf $(BUILD)
