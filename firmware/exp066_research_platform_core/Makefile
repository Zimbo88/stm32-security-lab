PROJECT := exp066_research_platform_core
BUILD := build
SRC := src
CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
OBJDUMP := arm-none-eabi-objdump
SIZE := arm-none-eabi-size
CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding -fno-builtin \
 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -fstack-usage -I$(SRC)
ASFLAGS := $(CPUFLAGS) -x assembler-with-cpp -g3
LDFLAGS := $(CPUFLAGS) -T linker.ld -nostdlib -Wl,--gc-sections \
 -Wl,-Map=$(BUILD)/$(PROJECT).map
SOURCES := $(wildcard $(SRC)/*.c)
OBJECTS := $(BUILD)/startup.o $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))
ELF := $(BUILD)/$(PROJECT).elf
BIN := $(BUILD)/$(PROJECT).bin
SIGNED := $(BUILD)/$(PROJECT)_signed.bin
SIGNER ?= ../exp065_signed_app/tools/build_signed_image.py
SIGNING_SEED ?= ../exp065_signed_app/keys/firmware_signing_seed.bin

.PHONY: all clean report signed verify-layout
all: $(BIN) verify-layout
	$(SIZE) $(ELF)
$(BUILD):
	mkdir -p $@
$(BUILD)/startup.o: $(SRC)/startup.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(ELF): $(OBJECTS) linker.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
verify-layout: $(ELF)
	@address=$$($(OBJDUMP) -h $(ELF) | awk '$$2 == ".isr_vector" {print $$4}'); \
	 test "$$address" = "08008200" || { echo "ERROR: vector at $$address"; exit 1; }
signed: $(BIN)
	python3 $(SIGNER) --application $(BIN) --seed $(SIGNING_SEED) --output $(SIGNED)
report: signed
	@wc -c $(BIN) $(SIGNED)
	@$(SIZE) -A $(ELF)
clean:
	rm -rf $(BUILD)
