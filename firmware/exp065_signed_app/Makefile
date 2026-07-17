PROJECT := exp065_signed_app
BUILD   := build
SRC     := src
COMMON  := ../common
include $(COMMON)/stm32f429_memory_layout.mk

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPUFLAGS := -mcpu=cortex-m4 -mthumb
REPRO_FLAGS := -ffile-prefix-map=$(CURDIR)=. \
               -fdebug-prefix-map=$(CURDIR)=. \
               -fmacro-prefix-map=$(CURDIR)=.
CFLAGS   := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding \
            -fdata-sections -ffunction-sections -fno-builtin \
            -Wall -Wextra -Werror $(REPRO_FLAGS) -I$(SRC) -I$(COMMON)
ASFLAGS  := $(CPUFLAGS) -x assembler-with-cpp -g3 $(REPRO_FLAGS)
LDFLAGS  := $(CPUFLAGS) -T linker.ld -nostdlib \
            -Wl,--gc-sections -Wl,--build-id=none \
            -Wl,-Map=$(BUILD)/$(PROJECT).map

SOURCES_C := $(sort $(wildcard $(SRC)/*.c))
OBJECTS_C := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES_C))
OBJECTS   := $(BUILD)/startup.o $(OBJECTS_C)

APPLICATION_ELF := $(BUILD)/$(PROJECT).elf
APPLICATION_BIN := $(BUILD)/$(PROJECT).bin
APPLICATION_HEX := $(BUILD)/$(PROJECT).hex
SIGNED_IMAGE    := $(BUILD)/$(PROJECT)_signed.bin
VERIFY_REPORT   := $(BUILD)/$(PROJECT)_verify.json
SIGNING_SEED    ?=
PUBLIC_KEY_HEX  ?=
PUBLIC_KEY_HEADER ?=

.PHONY: all clean signed verify-signed report require-signing-seed require-public-key

all: $(APPLICATION_ELF) $(APPLICATION_BIN) $(APPLICATION_HEX)
	$(SIZE) $(APPLICATION_ELF)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(APPLICATION_ELF): $(OBJECTS) linker.ld $(COMMON)/stm32f429_memory_layout.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(APPLICATION_BIN): $(APPLICATION_ELF)
	$(OBJCOPY) -O binary $< $@

$(APPLICATION_HEX): $(APPLICATION_ELF)
	$(OBJCOPY) -O ihex $< $@

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

signed: $(APPLICATION_BIN) require-signing-seed
	python3 tools/build_signed_image.py \
	    --application $(APPLICATION_BIN) \
	    --seed "$(SIGNING_SEED)" \
	    --output $(SIGNED_IMAGE)

verify-signed: signed require-public-key
	@key_arg=""; \
	if [ -n "$(PUBLIC_KEY_HEX)" ]; then \
		key_arg="--public-key-hex $(PUBLIC_KEY_HEX)"; \
	else \
		key_arg="--public-key-header $(PUBLIC_KEY_HEADER)"; \
	fi; \
	python3 ../../tools/release_artifacts.py verify-signed \
	    --signed-image $(SIGNED_IMAGE) \
	    --application $(APPLICATION_BIN) \
	    $$key_arg \
	    --json-output $(VERIFY_REPORT)

report: signed
	@echo
	@echo "Application binary:"
	@wc -c $(APPLICATION_BIN)
	@echo
	@echo "Signed image:"
	@wc -c $(SIGNED_IMAGE)
	@echo
	@$(SIZE) -A $(APPLICATION_ELF)

clean:
	rm -rf $(BUILD)
