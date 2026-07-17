PROJECT := exp045_bootloader_v2
BUILD   := build
SRC     := src

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS   := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding \
            -fdata-sections -ffunction-sections -fno-builtin \
            -Wall -Wextra -Werror -fstack-usage -I$(SRC)
ASFLAGS  := $(CPUFLAGS) -x assembler-with-cpp -g3
LDFLAGS  := $(CPUFLAGS) -T linker.ld -nostdlib \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(PROJECT).map

SOURCES_C := $(wildcard $(SRC)/*.c)
OBJECTS_C := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES_C))
OBJECTS   := $(BUILD)/startup.o $(OBJECTS_C)

.PHONY: all clean flash sizecheck

all: $(BUILD)/$(PROJECT).elf $(BUILD)/$(PROJECT).bin sizecheck
	$(SIZE) $(BUILD)/$(PROJECT).elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(PROJECT).elf: $(OBJECTS) linker.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

sizecheck: $(BUILD)/$(PROJECT).bin
	@size=$$(stat -c %s $<); \
	echo "Bootloader binary size: $$size bytes"; \
	if [ $$size -gt 32768 ]; then \
	    echo "FEHLER: Bootloader überschreitet 32 KiB."; \
	    exit 1; \
	fi

flash: all
	st-flash --reset write $(BUILD)/$(PROJECT).bin 0x08000000

clean:
	rm -rf $(BUILD)
