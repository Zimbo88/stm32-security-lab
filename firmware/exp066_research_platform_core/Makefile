PROJECT := exp066_research_platform_core
BUILD := build
SRC := src
COMMON := ../common
include $(COMMON)/stm32f429_memory_layout.mk
CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
OBJDUMP := arm-none-eabi-objdump
SIZE := arm-none-eabi-size
CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding -fno-builtin \
 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -fstack-usage -I$(SRC) -I$(COMMON)
ASFLAGS := $(CPUFLAGS) -x assembler-with-cpp -g3
LDFLAGS := $(CPUFLAGS) -T linker.ld -nostdlib -Wl,--gc-sections \
 -Wl,-Map=$(BUILD)/$(PROJECT).map
SOURCES := $(wildcard $(SRC)/*.c)
OBJECTS := $(BUILD)/startup.o $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))
ELF := $(BUILD)/$(PROJECT).elf
BIN := $(BUILD)/$(PROJECT).bin
SIGNED := $(BUILD)/$(PROJECT)_signed.bin
SIGNER ?= ../exp065_signed_app/tools/build_signed_image.py
SIGNING_SEED ?=

.PHONY: all clean report signed verify-layout require-signing-seed
all: $(BIN) verify-layout
	$(SIZE) $(ELF)
$(BUILD):
	mkdir -p $@
$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(ELF): $(OBJECTS) linker.ld $(COMMON)/stm32f429_memory_layout.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
verify-layout: $(ELF)
	@address=$$($(OBJDUMP) -h $(ELF) | awk '$$2 == ".isr_vector" {print $$4}'); \
	 test "$$address" = "$(STM32F429_APPLICATION_BASE_HEX)" || { echo "ERROR: vector at $$address"; exit 1; }
require-signing-seed:
	@test -n "$(SIGNING_SEED)" || { \
		echo "ERROR: set SIGNING_SEED to an explicit Ed25519 seed path"; \
		exit 1; \
	}
	@test -f "$(SIGNING_SEED)" || { \
		echo "ERROR: SIGNING_SEED does not name a file: $(SIGNING_SEED)"; \
		exit 1; \
	}
signed: $(BIN) require-signing-seed
	python3 $(SIGNER) --application $(BIN) --seed "$(SIGNING_SEED)" --output $(SIGNED)
report: signed
	@wc -c $(BIN) $(SIGNED)
	@$(SIZE) -A $(ELF)
clean:
	rm -rf $(BUILD)
