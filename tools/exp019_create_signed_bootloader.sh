#!/usr/bin/env bash
set -euo pipefail

LAB="${HOME}/stm32-security-lab"
PROJECT="${LAB}/firmware/exp019_signed_bootloader"
MONO="${LAB}/third_party/Monocypher-4.0.3/src"
KEY_HEADER="${LAB}/signing/exp017/firmware_public_key.h"
SIGNED_IMAGE="${LAB}/signing/exp018/exp018_signed_application_image.bin"

for file in \
    "${MONO}/monocypher.c" \
    "${MONO}/monocypher.h" \
    "${MONO}/optional/monocypher-ed25519.c" \
    "${MONO}/optional/monocypher-ed25519.h" \
    "${KEY_HEADER}" \
    "${SIGNED_IMAGE}"
do
    if [[ ! -f "${file}" ]]; then
        echo "FEHLER: benötigte Datei fehlt: ${file}" >&2
        exit 1
    fi
done

rm -rf "${PROJECT}"
mkdir -p "${PROJECT}/src"

cp "${MONO}/monocypher.c" "${PROJECT}/src/"
cp "${MONO}/monocypher.h" "${PROJECT}/src/"
cp "${MONO}/optional/monocypher-ed25519.c" "${PROJECT}/src/"
cp "${MONO}/optional/monocypher-ed25519.h" "${PROJECT}/src/"
cp "${KEY_HEADER}" "${PROJECT}/src/"

cat > "${PROJECT}/src/startup.s" <<'EOF'
.syntax unified
.cpu cortex-m4
.thumb

.global Reset_Handler
.global Default_Handler

.extern main
.extern _estack
.extern _etext
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss

.section .isr_vector, "a", %progbits
vector_table:
    .word _estack
    .word Reset_Handler
    .rept 14
    .word Default_Handler
    .endr

.section .text.Reset_Handler, "ax", %progbits
.thumb_func
Reset_Handler:
    ldr r0, =_etext
    ldr r1, =_sdata
    ldr r2, =_edata
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b

2:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
3:
    cmp r0, r1
    bcs 4f
    str r2, [r0], #4
    b 3b

4:
    bl main
5:
    b 5b

.section .text.Default_Handler, "ax", %progbits
.thumb_func
Default_Handler:
    b Default_Handler
EOF

cat > "${PROJECT}/src/uart.h" <<'EOF'
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init_115200_hsi16(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_put_hex32(uint32_t value);
void uart_put_u32(uint32_t value);

#endif
EOF

cat > "${PROJECT}/src/uart.c" <<'EOF'
#include "uart.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define RCC_AHB1ENR   REG32(0x40023830UL)
#define RCC_APB2ENR   REG32(0x40023844UL)

#define GPIOA_MODER   REG32(0x40020000UL)
#define GPIOA_OTYPER  REG32(0x40020004UL)
#define GPIOA_OSPEEDR REG32(0x40020008UL)
#define GPIOA_PUPDR   REG32(0x4002000CUL)
#define GPIOA_AFRH    REG32(0x40020024UL)

#define USART1_SR     REG32(0x40011000UL)
#define USART1_DR     REG32(0x40011004UL)
#define USART1_BRR    REG32(0x40011008UL)
#define USART1_CR1    REG32(0x4001100CUL)
#define USART1_CR2    REG32(0x40011010UL)
#define USART1_CR3    REG32(0x40011014UL)

#define USART_SR_TXE  (1UL << 7)
#define USART_CR1_UE  (1UL << 13)
#define USART_CR1_TE  (1UL << 3)
#define USART_CR1_RE  (1UL << 2)

void uart_init_115200_hsi16(void)
{
    RCC_AHB1ENR |= (1UL << 0);
    RCC_APB2ENR |= (1UL << 4);
    (void)RCC_AHB1ENR;
    (void)RCC_APB2ENR;

    GPIOA_MODER &= ~((3UL << 18) | (3UL << 20));
    GPIOA_MODER |=  ((2UL << 18) | (2UL << 20));

    GPIOA_OTYPER &= ~((1UL << 9) | (1UL << 10));
    GPIOA_OSPEEDR |= ((2UL << 18) | (2UL << 20));

    GPIOA_PUPDR &= ~((3UL << 18) | (3UL << 20));
    GPIOA_PUPDR |=  (1UL << 20);

    GPIOA_AFRH &= ~((0xFUL << 4) | (0xFUL << 8));
    GPIOA_AFRH |=  ((7UL << 4) | (7UL << 8));

    USART1_CR1 = 0U;
    USART1_CR2 = 0U;
    USART1_CR3 = 0U;
    USART1_BRR = 0x008BUL;
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(char c)
{
    while ((USART1_SR & USART_SR_TXE) == 0U) {
    }
    USART1_DR = (uint32_t)(uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

void uart_put_hex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> (uint32_t)shift) & 0xFUL]);
    }
}

void uart_put_u32(uint32_t value)
{
    char buffer[10];
    uint32_t count = 0U;

    if (value == 0U) {
        uart_putc('0');
        return;
    }

    while (value != 0U) {
        buffer[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (count != 0U) {
        uart_putc(buffer[--count]);
    }
}
EOF

cat > "${PROJECT}/src/delay.h" <<'EOF'
#ifndef DELAY_H
#define DELAY_H
#include <stdint.h>
void delay_cycles(uint32_t cycles);
#endif
EOF

cat > "${PROJECT}/src/delay.c" <<'EOF'
#include "delay.h"

void delay_cycles(uint32_t cycles)
{
    volatile uint32_t n = cycles;
    while (n-- != 0U) {
        __asm volatile ("nop");
    }
}
EOF

cat > "${PROJECT}/src/signed_image.h" <<'EOF'
#ifndef SIGNED_IMAGE_H
#define SIGNED_IMAGE_H

#include <stdint.h>

#define SIGNED_IMAGE_BASE      0x08008000UL
#define SIGNED_MANIFEST_SIZE   96UL
#define SIGNATURE_ADDRESS      (SIGNED_IMAGE_BASE + 0x60UL)
#define PAYLOAD_ADDRESS        0x08008200UL
#define SIGNED_IMAGE_MAGIC     0x31474953UL
#define SIGNED_HEADER_VERSION  1UL
#define MAX_PAYLOAD_SIZE       (0x08100000UL - PAYLOAD_ADDRESS)

typedef struct {
    uint32_t magic;
    uint32_t header_version;
    uint32_t image_version;
    uint32_t vector_address;
    uint32_t image_size;
    uint32_t flags;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t payload_sha512[64];
} signed_manifest_t;

typedef enum {
    VERIFY_OK = 0,
    VERIFY_BAD_MAGIC,
    VERIFY_BAD_HEADER_VERSION,
    VERIFY_BAD_VECTOR_ADDRESS,
    VERIFY_BAD_SIZE,
    VERIFY_BAD_STACK,
    VERIFY_BAD_RESET_VECTOR,
    VERIFY_BAD_PAYLOAD_HASH,
    VERIFY_BAD_SIGNATURE
} verify_status_t;

verify_status_t signed_image_verify(void);
const char *signed_image_status_text(verify_status_t status);
void signed_image_jump(void);

#endif
EOF

cat > "${PROJECT}/src/signed_image.c" <<'EOF'
#include <stddef.h>
#include <stdint.h>

#include "firmware_public_key.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "signed_image.h"

#define REG32(a) (*(volatile uint32_t *)(a))

#define SCB_VTOR       REG32(0xE000ED08UL)
#define SYST_CSR       REG32(0xE000E010UL)
#define NVIC_ICER_BASE 0xE000E180UL
#define NVIC_ICPR_BASE 0xE000E280UL

typedef void (*entry_fn_t)(void);

static const signed_manifest_t *manifest(void)
{
    return (const signed_manifest_t *)SIGNED_IMAGE_BASE;
}

static const uint8_t *signature(void)
{
    return (const uint8_t *)SIGNATURE_ADDRESS;
}

verify_status_t signed_image_verify(void)
{
    const signed_manifest_t *m = manifest();

    if (sizeof(signed_manifest_t) != SIGNED_MANIFEST_SIZE) {
        return VERIFY_BAD_HEADER_VERSION;
    }

    if (m->magic != SIGNED_IMAGE_MAGIC) {
        return VERIFY_BAD_MAGIC;
    }

    if (m->header_version != SIGNED_HEADER_VERSION) {
        return VERIFY_BAD_HEADER_VERSION;
    }

    if (m->vector_address != PAYLOAD_ADDRESS) {
        return VERIFY_BAD_VECTOR_ADDRESS;
    }

    if ((m->image_size < 8U) || (m->image_size > MAX_PAYLOAD_SIZE)) {
        return VERIFY_BAD_SIZE;
    }

    const uint32_t msp =
        *(volatile const uint32_t *)(m->vector_address + 0U);
    const uint32_t reset =
        *(volatile const uint32_t *)(m->vector_address + 4U);

    if ((msp < 0x20000000UL) || (msp > 0x20020000UL)) {
        return VERIFY_BAD_STACK;
    }

    if (((reset & 1UL) == 0U) ||
        ((reset & ~1UL) < m->vector_address) ||
        ((reset & ~1UL) >= (m->vector_address + m->image_size))) {
        return VERIFY_BAD_RESET_VECTOR;
    }

    uint8_t computed_hash[64];
    crypto_sha512(
        computed_hash,
        (const uint8_t *)m->vector_address,
        (size_t)m->image_size
    );

    if (crypto_verify64(computed_hash, m->payload_sha512) != 0) {
        crypto_wipe(computed_hash, sizeof(computed_hash));
        return VERIFY_BAD_PAYLOAD_HASH;
    }

    crypto_wipe(computed_hash, sizeof(computed_hash));

    if (crypto_ed25519_check(
            signature(),
            firmware_public_key,
            (const uint8_t *)SIGNED_IMAGE_BASE,
            (size_t)SIGNED_MANIFEST_SIZE) != 0) {
        return VERIFY_BAD_SIGNATURE;
    }

    return VERIFY_OK;
}

const char *signed_image_status_text(verify_status_t status)
{
    switch (status) {
    case VERIFY_OK:                 return "OK";
    case VERIFY_BAD_MAGIC:          return "BAD MAGIC";
    case VERIFY_BAD_HEADER_VERSION: return "BAD HEADER VERSION";
    case VERIFY_BAD_VECTOR_ADDRESS: return "BAD VECTOR ADDRESS";
    case VERIFY_BAD_SIZE:           return "BAD IMAGE SIZE";
    case VERIFY_BAD_STACK:          return "BAD INITIAL MSP";
    case VERIFY_BAD_RESET_VECTOR:   return "BAD RESET VECTOR";
    case VERIFY_BAD_PAYLOAD_HASH:   return "PAYLOAD SHA512 MISMATCH";
    case VERIFY_BAD_SIGNATURE:      return "ED25519 SIGNATURE INVALID";
    default:                        return "UNKNOWN";
    }
}

void signed_image_jump(void)
{
    const signed_manifest_t *m = manifest();
    const uint32_t new_msp =
        *(volatile const uint32_t *)(m->vector_address + 0U);
    const uint32_t reset =
        *(volatile const uint32_t *)(m->vector_address + 4U);
    const entry_fn_t entry = (entry_fn_t)reset;

    __asm volatile ("cpsid i" ::: "memory");

    SYST_CSR = 0U;

    for (uint32_t i = 0U; i < 8U; ++i) {
        REG32(NVIC_ICER_BASE + (i * 4U)) = 0xFFFFFFFFUL;
        REG32(NVIC_ICPR_BASE + (i * 4U)) = 0xFFFFFFFFUL;
    }

    SCB_VTOR = m->vector_address;

    __asm volatile (
        "dsb\n"
        "isb\n"
        "msr msp, %0\n"
        "cpsie i\n"
        :
        : "r" (new_msp)
        : "memory"
    );

    entry();

    for (;;) {
    }
}
EOF

cat > "${PROJECT}/src/main.c" <<'EOF'
#include <stdint.h>

#include "delay.h"
#include "signed_image.h"
#include "uart.h"

int main(void)
{
    const signed_manifest_t *m =
        (const signed_manifest_t *)SIGNED_IMAGE_BASE;

    uart_init_115200_hsi16();

    uart_puts("\n========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP019 ED25519 SIGNED BOOT\n");
    uart_puts("========================================\n");

    uart_puts("Manifest address = ");
    uart_put_hex32(SIGNED_IMAGE_BASE);
    uart_puts("\nMagic            = ");
    uart_put_hex32(m->magic);
    uart_puts("\nHeader version   = ");
    uart_put_u32(m->header_version);
    uart_puts("\nImage version    = ");
    uart_put_u32(m->image_version);
    uart_puts("\nVector address   = ");
    uart_put_hex32(m->vector_address);
    uart_puts("\nImage size       = ");
    uart_put_u32(m->image_size);
    uart_puts(" bytes\n");

    uart_puts("Computing SHA-512 and verifying Ed25519...\n");

    const verify_status_t status = signed_image_verify();

    uart_puts("Verification     = ");
    uart_puts(signed_image_status_text(status));
    uart_puts("\n");

    if (status != VERIFY_OK) {
        uart_puts("Application will NOT be started.\n");
        uart_puts("Bootloader halted safely.\n");
        for (;;) {
        }
    }

    uart_puts("Signature and payload hash accepted.\n");
    uart_puts("Jumping to application...\n");
    delay_cycles(4000000U);

    signed_image_jump();

    for (;;) {
    }
}
EOF

cat > "${PROJECT}/linker.ld" <<'EOF'
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data : AT(_etext)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM
}
EOF

cat > "${PROJECT}/Makefile" <<'EOF'
PROJECT := exp019_signed_bootloader
BUILD   := build
SRC     := src

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CPUFLAGS := -mcpu=cortex-m4 -mthumb
CFLAGS   := $(CPUFLAGS) -std=c11 -Os -g3 -ffreestanding \
            -fdata-sections -ffunction-sections -fno-builtin \
            -Wall -Wextra -Werror -I$(SRC)
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
EOF

cat > "${PROJECT}/README.md" <<'EOF'
# EXP019 – Ed25519 signed bootloader

This bootloader verifies:

1. signed-image metadata and bounds;
2. SHA-512 of the application payload;
3. Ed25519 signature over the fixed 96-byte manifest;
4. application vector table and initial MSP.

Flash layout:

- 0x08000000–0x08007FFF: bootloader (sectors 0 and 1, max 32 KiB)
- 0x08008000: signed image header
- 0x08008200: application vector table and payload

The private signing key is not included.
EOF

echo "Projekt erzeugt: ${PROJECT}"
echo
echo "Nächste Befehle:"
echo "  cd ${PROJECT}"
echo "  make clean"
echo "  make"
echo
echo "Noch NICHT flashen. Zuerst Größe und Symbole prüfen."
