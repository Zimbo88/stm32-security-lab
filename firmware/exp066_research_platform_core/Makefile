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
REPRO_FLAGS := -ffile-prefix-map=$(CURDIR)=. \
 -fdebug-prefix-map=$(CURDIR)=. \
 -fmacro-prefix-map=$(CURDIR)=.
CFLAGS := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding -fno-builtin \
 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -fstack-usage \
 $(REPRO_FLAGS) -I$(SRC) -I$(COMMON)
ASFLAGS := $(CPUFLAGS) -x assembler-with-cpp -g3 $(REPRO_FLAGS)
LDFLAGS := $(CPUFLAGS) -T linker.ld -nostdlib -Wl,--gc-sections \
 -Wl,--build-id=none -Wl,-Map=$(BUILD)/$(PROJECT).map
SOURCES := $(sort $(wildcard $(SRC)/*.c))
OBJECTS := $(BUILD)/startup.o $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))
ELF := $(BUILD)/$(PROJECT).elf
BIN := $(BUILD)/$(PROJECT).bin
HEX := $(BUILD)/$(PROJECT).hex
SIGNED := $(BUILD)/$(PROJECT)_signed.bin
VERIFY_REPORT := $(BUILD)/$(PROJECT)_verify.json
SIGNER ?= ../exp065_signed_app/tools/build_signed_image.py
SIGNING_SEED ?=
PUBLIC_KEY_HEX ?=
PUBLIC_KEY_HEADER ?=

.PHONY: all clean report signed verify-signed verify-layout require-signing-seed require-public-key
all: $(BIN) $(HEX) verify-layout
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
$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@
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
require-public-key:
	@if [ -n "$(PUBLIC_KEY_HEX)" ] && [ -n "$(PUBLIC_KEY_HEADER)" ]; then \
		echo "ERROR: set only one of PUBLIC_KEY_HEX or PUBLIC_KEY_HEADER"; \
		exit 1; \
	fi
	@if [ -z "$(PUBLIC_KEY_HEX)" ] && [ -z "$(PUBLIC_KEY_HEADER)" ]; then \
		echo "ERROR: set PUBLIC_KEY_HEX or PUBLIC_KEY_HEADER for verification"; \
		exit 1; \
	fi
signed: $(BIN) require-signing-seed
	python3 $(SIGNER) --application $(BIN) --seed "$(SIGNING_SEED)" --output $(SIGNED)
verify-signed: signed require-public-key
	@key_arg=""; \
	if [ -n "$(PUBLIC_KEY_HEX)" ]; then \
		key_arg="--public-key-hex $(PUBLIC_KEY_HEX)"; \
	else \
		key_arg="--public-key-header $(PUBLIC_KEY_HEADER)"; \
	fi; \
	python3 ../../tools/release_artifacts.py verify-signed \
	    --signed-image $(SIGNED) \
	    --application $(BIN) \
	    $$key_arg \
	    --json-output $(VERIFY_REPORT)
report: signed
	@wc -c $(BIN) $(SIGNED)
	@$(SIZE) -A $(ELF)
clean:
	rm -rf $(BUILD)
