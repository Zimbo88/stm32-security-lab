PROJECT := exp066_research_platform_core
BUILD := build
SRC := src
COMMON := ../common
BOOTLOADER := ../exp045_bootloader_v2
include $(COMMON)/stm32f429_memory_layout.mk
LAYOUT_PROFILE ?= stm32f429_1m
RSM_ENABLE ?= 1
RSM_RESTRICTED_DIAGNOSTICS ?= 0
RSM_VECTOR_MONITOR_ENABLE ?= 1
ifneq ($(LAYOUT_PROFILE),$(STM32F429_LAYOUT_PROFILE))
$(error generated layout profile $(STM32F429_LAYOUT_PROFILE) does not match requested $(LAYOUT_PROFILE))
endif
CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
OBJDUMP := arm-none-eabi-objdump
SIZE := arm-none-eabi-size
SLOT ?= a
ifeq ($(SLOT),a)
SLOT_APPLICATION_BASE_HEX := $(STM32F429_SLOT_A_APPLICATION_BASE_HEX)
SLOT_SIGNED_IMAGE_BASE_HEX := $(STM32F429_SLOT_A_SIGNED_IMAGE_BASE_HEX)
SLOT_END_HEX := $(STM32F429_SLOT_A_END_HEX)
SLOT_PAYLOAD_MAX_SIZE_BYTES := $(STM32F429_SLOT_A_PAYLOAD_MAX_SIZE_BYTES)
else ifeq ($(SLOT),b)
SLOT_APPLICATION_BASE_HEX := $(STM32F429_SLOT_B_APPLICATION_BASE_HEX)
SLOT_SIGNED_IMAGE_BASE_HEX := $(STM32F429_SLOT_B_SIGNED_IMAGE_BASE_HEX)
SLOT_END_HEX := $(STM32F429_SLOT_B_END_HEX)
SLOT_PAYLOAD_MAX_SIZE_BYTES := $(STM32F429_SLOT_B_PAYLOAD_MAX_SIZE_BYTES)
else
$(error SLOT must be a or b)
endif
CPUFLAGS := -mcpu=cortex-m4 -mthumb
REPRO_FLAGS := -ffile-prefix-map=$(CURDIR)=. \
 -fdebug-prefix-map=$(CURDIR)=. \
 -fmacro-prefix-map=$(CURDIR)=.
CFLAGS := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding -fno-builtin \
	 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -fstack-usage \
	 $(REPRO_FLAGS) -DPLATFORM_APP_BASE_OVERRIDE=0x$(SLOT_APPLICATION_BASE_HEX)UL \
	 -DRSM_ENABLE=$(RSM_ENABLE) -DRSM_RESTRICTED_DIAGNOSTICS=$(RSM_RESTRICTED_DIAGNOSTICS) \
	 -DRSM_VECTOR_MONITOR_ENABLE=$(RSM_VECTOR_MONITOR_ENABLE) \
	 -I$(SRC) -I$(COMMON) -I$(BOOTLOADER)/src -I$(BOOTLOADER)/include
ASFLAGS := $(CPUFLAGS) -x assembler-with-cpp -g3 $(REPRO_FLAGS)
LDFLAGS := $(CPUFLAGS) -T linker.ld -nostdlib -Wl,--gc-sections \
 -Wl,--build-id=none -Wl,-Map=$(BUILD)/$(PROJECT).map \
 -Wl,--defsym=STM32F429_SELECTED_APPLICATION_BASE=0x$(SLOT_APPLICATION_BASE_HEX) \
 -Wl,--defsym=STM32F429_SELECTED_APPLICATION_PAYLOAD_MAX_SIZE=$(SLOT_PAYLOAD_MAX_SIZE_BYTES)
SOURCES := $(sort $(wildcard $(SRC)/*.c))
BOOTLOADER_SOURCES := \
	$(BOOTLOADER)/src/boot_confirmation.c \
	$(BOOTLOADER)/src/boot_flash.c \
	$(BOOTLOADER)/src/boot_flash_target.c \
	$(BOOTLOADER)/src/boot_metadata.c \
	$(BOOTLOADER)/src/boot_slot.c \
	$(BOOTLOADER)/src/minilib.c
OBJECTS := $(BUILD)/startup.o \
	$(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES)) \
	$(patsubst $(BOOTLOADER)/src/%.c,$(BUILD)/bootloader_%.o,$(BOOTLOADER_SOURCES))
ELF := $(BUILD)/$(PROJECT).elf
BIN := $(BUILD)/$(PROJECT).bin
HEX := $(BUILD)/$(PROJECT).hex
LEGACY_SIGNED := $(BUILD)/$(PROJECT)_signed.bin
UPDATE_PACKAGE := $(BUILD)/$(PROJECT)_slot_$(SLOT)_update_v2.bin
UPDATE_PACKAGE_BUILD_REPORT := $(BUILD)/$(PROJECT)_slot_$(SLOT)_package_build.json
UPDATE_PACKAGE_INSPECT_REPORT := $(BUILD)/$(PROJECT)_slot_$(SLOT)_release_manifest.json
UPDATE_PACKAGE_VERIFY_REPORT := $(BUILD)/$(PROJECT)_slot_$(SLOT)_package_verify.json
SIGNED := $(UPDATE_PACKAGE)
VERIFY_REPORT := $(UPDATE_PACKAGE_VERIFY_REPORT)
UPDATE_PACKAGE_TOOL ?= ../../tools/update_package.py
SIGNING_SEED ?=
PUBLIC_KEY_HEX ?=
PUBLIC_KEY_HEADER ?=
ifneq ($(strip $(SIGNING_SEED)),)
SIGNING_SEED_DEP := $(SIGNING_SEED)
endif
ifneq ($(strip $(PUBLIC_KEY_HEADER)),)
PUBLIC_KEY_VERIFY_DEPS := $(PUBLIC_KEY_HEADER)
endif

.PHONY: all clean report signed verify-signed update-package inspect-update-package verify-update-package verify-layout require-signing-seed require-public-key slot-a slot-b slot-releases FORCE
all: $(BIN) $(HEX) verify-layout
	@echo "MCU: $(STM32F429_MCU)"
	@echo "Layout profile: $(STM32F429_LAYOUT_PROFILE)"
	@echo "Flash: 0x$(STM32F429_FLASH_BASE_HEX)-0x$(STM32F429_FLASH_END_HEX) ($(STM32F429_FLASH_SIZE_KIB) KiB)"
	@echo "Slot $(SLOT): signed image 0x$(SLOT_SIGNED_IMAGE_BASE_HEX), payload 0x$(SLOT_APPLICATION_BASE_HEX), end 0x$(SLOT_END_HEX)"
	$(SIZE) $(ELF)
$(BUILD):
	mkdir -p $@
$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/bootloader_%.o: $(BOOTLOADER)/src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(ELF): $(OBJECTS) linker.ld $(COMMON)/stm32f429_memory_layout.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $< $@
verify-layout: $(ELF)
	@address=$$($(OBJDUMP) -h $(ELF) | awk '$$2 == ".isr_vector" {print $$4}'); \
	 test "$$address" = "$(SLOT_APPLICATION_BASE_HEX)" || { echo "ERROR: vector at $$address"; exit 1; }
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
signed: update-package
verify-signed: verify-update-package
update-package: require-signing-seed $(UPDATE_PACKAGE) $(UPDATE_PACKAGE_BUILD_REPORT)
inspect-update-package: $(UPDATE_PACKAGE_INSPECT_REPORT)
verify-update-package: $(UPDATE_PACKAGE_VERIFY_REPORT)
$(UPDATE_PACKAGE) $(UPDATE_PACKAGE_BUILD_REPORT) &: $(BIN) $(UPDATE_PACKAGE_TOOL) $(SIGNING_SEED_DEP) | $(BUILD)
	@test -n "$(SIGNING_SEED)" || { \
		echo "ERROR: set SIGNING_SEED to an explicit Ed25519 seed path"; \
		exit 1; \
	}
	@test -f "$(SIGNING_SEED)" || { \
		echo "ERROR: SIGNING_SEED does not name a file: $(SIGNING_SEED)"; \
		exit 1; \
	}
	@if [ "$(LEGACY_SIGNED)" != "$(UPDATE_PACKAGE)" ] && [ -e "$(LEGACY_SIGNED)" ]; then \
		echo "Removing legacy EXP066 signed-image artifact: $(LEGACY_SIGNED)"; \
		rm -f "$(LEGACY_SIGNED)"; \
	fi
	rm -f $(UPDATE_PACKAGE_INSPECT_REPORT) $(UPDATE_PACKAGE_VERIFY_REPORT)
	python3 $(UPDATE_PACKAGE_TOOL) build \
	    --application $(BIN) \
	    --seed "$(SIGNING_SEED)" \
	    --slot $(SLOT) \
	    --output $(UPDATE_PACKAGE) \
	    --json-output $(UPDATE_PACKAGE_BUILD_REPORT)
$(UPDATE_PACKAGE_INSPECT_REPORT): $(UPDATE_PACKAGE) $(UPDATE_PACKAGE_TOOL)
	@echo "Inspecting update package:"
	@echo "  package: $(UPDATE_PACKAGE)"
	@echo "  report : $(UPDATE_PACKAGE_INSPECT_REPORT)"
	python3 $(UPDATE_PACKAGE_TOOL) inspect \
	    --package $(UPDATE_PACKAGE) \
	    --json-output $(UPDATE_PACKAGE_INSPECT_REPORT)
$(UPDATE_PACKAGE_VERIFY_REPORT): $(UPDATE_PACKAGE) $(BIN) $(UPDATE_PACKAGE_TOOL) $(PUBLIC_KEY_VERIFY_DEPS) FORCE
	@key_arg=""; \
	key_value=""; \
	if [ -n "$(PUBLIC_KEY_HEX)" ]; then \
		key_arg="--public-key-hex"; \
		key_value="$(PUBLIC_KEY_HEX)"; \
	else \
		key_arg="--public-key-header"; \
		key_value="$(PUBLIC_KEY_HEADER)"; \
	fi; \
	if [ -n "$(PUBLIC_KEY_HEX)" ] && [ -n "$(PUBLIC_KEY_HEADER)" ]; then \
		echo "ERROR: set only one of PUBLIC_KEY_HEX or PUBLIC_KEY_HEADER"; \
		exit 1; \
	fi; \
	if [ -z "$(PUBLIC_KEY_HEX)" ] && [ -z "$(PUBLIC_KEY_HEADER)" ]; then \
		echo "ERROR: set PUBLIC_KEY_HEX or PUBLIC_KEY_HEADER for verification"; \
		exit 1; \
	fi; \
	echo "Verifying update package:"; \
	echo "  package    : $(UPDATE_PACKAGE)"; \
	echo "  application: $(BIN)"; \
	echo "  slot       : $(SLOT)"; \
	echo "  key source : $$key_arg $$key_value"; \
	echo "  report     : $(UPDATE_PACKAGE_VERIFY_REPORT)"; \
	if [ "$$key_arg" = "--public-key-header" ] && [ ! -f "$$key_value" ]; then \
		echo "ERROR: PUBLIC_KEY_HEADER does not name a file: $$key_value"; \
		exit 1; \
	fi; \
	python3 $(UPDATE_PACKAGE_TOOL) verify \
	    --package $(UPDATE_PACKAGE) \
	    --slot $(SLOT) \
	    --application $(BIN) \
	    $$key_arg "$$key_value" \
	    --json-output $(UPDATE_PACKAGE_VERIFY_REPORT)
report: signed
	@wc -c $(BIN) $(SIGNED)
	@$(SIZE) -A $(ELF)
slot-a:
	$(MAKE) SLOT=a BUILD=build/slot_a PROJECT=$(PROJECT)_slot_a clean all update-package inspect-update-package verify-update-package
slot-b:
	$(MAKE) SLOT=b BUILD=build/slot_b PROJECT=$(PROJECT)_slot_b clean all update-package inspect-update-package verify-update-package
slot-releases: slot-a slot-b
clean:
	rm -rf $(BUILD)
FORCE:
