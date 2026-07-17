PROJECT := exp065_signed_app
BUILD   := build
SRC     := src
COMMON  := ../common
include $(COMMON)/stm32f429_memory_layout.mk

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS   := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding \
            -fdata-sections -ffunction-sections -fno-builtin \
            -Wall -Wextra -Werror -I$(SRC) -I$(COMMON)
ASFLAGS  := $(CPUFLAGS) -x assembler-with-cpp -g3
LDFLAGS  := $(CPUFLAGS) -T linker.ld -nostdlib \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(PROJECT).map

SOURCES_C := $(wildcard $(SRC)/*.c)
OBJECTS_C := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES_C))
OBJECTS   := $(BUILD)/startup.o $(OBJECTS_C)

APPLICATION_BIN := $(BUILD)/$(PROJECT).bin
SIGNED_IMAGE    := $(BUILD)/$(PROJECT)_signed.bin
SIGNING_SEED    := keys/firmware_signing_seed.bin

.PHONY: all clean signed report

all: $(APPLICATION_BIN)
	$(SIZE) $(BUILD)/$(PROJECT).elf

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(PROJECT).elf: $(OBJECTS) linker.ld $(COMMON)/stm32f429_memory_layout.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(APPLICATION_BIN): $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

signed: $(APPLICATION_BIN) $(SIGNING_SEED)
	python3 tools/build_signed_image.py \
	    --application $(APPLICATION_BIN) \
	    --seed $(SIGNING_SEED) \
	    --output $(SIGNED_IMAGE)

report: signed
	@echo
	@echo "Application binary:"
	@wc -c $(APPLICATION_BIN)
	@echo
	@echo "Signed image:"
	@wc -c $(SIGNED_IMAGE)
	@echo
	@$(SIZE) -A $(BUILD)/$(PROJECT).elf

clean:
	rm -rf $(BUILD)
