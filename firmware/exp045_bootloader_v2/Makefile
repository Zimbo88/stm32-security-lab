PROJECT := exp045_bootloader_v2
BUILD   := build
SRC     := src
COMMON  := ../common
include $(COMMON)/stm32f429_memory_layout.mk

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size
NM      := arm-none-eabi-nm

CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS   := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding \
            -fdata-sections -ffunction-sections -fno-builtin \
            -Wall -Wextra -Werror -fstack-usage -I$(SRC) -Iinclude -I$(COMMON)
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

$(BUILD)/$(PROJECT).elf: $(OBJECTS) linker.ld $(COMMON)/stm32f429_memory_layout.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

sizecheck: $(BUILD)/$(PROJECT).bin
	@size=$$(stat -c %s $<); \
	echo "Bootloader binary size: $$size bytes"; \
	if [ $$size -gt $(STM32F429_BOOTLOADER_SIZE_BYTES) ]; then \
	    echo "FEHLER: Bootloader überschreitet 32 KiB."; \
	    exit 1; \
	fi

flash: all
	st-flash --reset write $(BUILD)/$(PROJECT).bin 0x08000000

clean:
	rm -rf $(BUILD)

# EXP053: Keep the bootloader inside the reserved 32 KiB flash region.
BOOTLOADER_MAX_SIZE_BYTES := $(STM32F429_BOOTLOADER_SIZE_BYTES)
BOOTLOADER_BINARY := $(BUILD)/$(PROJECT).bin

.PHONY: check-size
check-size: $(BOOTLOADER_BINARY)
	@actual_size=$$(wc -c < "$(BOOTLOADER_BINARY)"); \
	echo "Bootloader size check: $$actual_size / $(BOOTLOADER_MAX_SIZE_BYTES) bytes"; \
	if [ "$$actual_size" -gt "$(BOOTLOADER_MAX_SIZE_BYTES)" ]; then \
		echo "ERROR: bootloader exceeds the reserved 32 KiB region"; \
		exit 1; \
	fi

all: check-size

.PHONY: report
report: all
	@echo
	@echo "=== Bootloader build report ==="
	@echo "Project          : $(PROJECT)"
	@echo "Binary           : $(BOOTLOADER_BINARY)"
	@actual_size=$$(wc -c < "$(BOOTLOADER_BINARY)"); \
	remaining=$$(( $(BOOTLOADER_MAX_SIZE_BYTES) - actual_size )); \
	echo "Binary size      : $$actual_size bytes"; \
	echo "Reserved size    : $(BOOTLOADER_MAX_SIZE_BYTES) bytes"; \
	echo "Remaining space  : $$remaining bytes"
	@echo
	@$(SIZE) -A $(BUILD)/$(PROJECT).elf
	@echo
	@echo "Flash symbols:"
	@$(NM) -n $(BUILD)/$(PROJECT).elf | \
	grep -E '_flash_region_start|_flash_image_end|_flash_region_end'
