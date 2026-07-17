# EXP060 – Board and Software Coverage Audit

## Scope

Compared the uploaded STM32F429IGT6 board schematic with the current project
`firmware/exp045_bootloader_v2`.

## Current software coverage

Implemented and used:

- Cortex-M4 startup and vector table
- internal Flash execution
- basic internal SRAM startup area
- USART1 on PA9/PA10 at 115200 baud, assuming 16 MHz HSI
- reset-cause capture through RCC_CSR
- Ed25519 signature verification and SHA-512 payload verification
- fixed minimum image version policy
- application vector validation and jump
- four active-low LEDs on PE3, PH10, PH11 and PH12 in the halt path

Not implemented in the current project:

- 25 MHz HSE and PLL clock tree
- 32.768 kHz LSE and RTC
- external SDRAM W9825G6KH-6
- external NAND W29N01HVSINA
- USB FS device on PA11/PA12
- button inputs on PA0, PC13, PE2 and PI11
- CRC peripheral
- RNG peripheral
- DMA tests
- timer tests
- watchdog tests
- ADC internal reference/temperature tests
- persistent diagnostic report
- functional recovery input and recovery transport

## Important findings

### 1. Only half of the internal main SRAM is described

The linker script declares 128 KiB at `0x20000000`. The STM32F429IGT6 has more
internal SRAM available, including memory not represented by the current linker
layout. The application stack validation also stops at `0x20020000`, so images
using a wider valid SRAM range may be rejected.

### 2. Only the first Flash bank is assigned to application payloads

`APPLICATION_FLASH_END` is `0x08100000`. On the detected 2 MiB device this leaves
the second 1 MiB bank outside the signed payload policy.

### 3. Bootloader protection and bootloader size do not currently match

The bootloader reserves 32 KiB (`0x08000000` through `0x08007FFF`), which spans
two 16 KiB sectors. The diagnostic log showed only sector 0 protected. Sector 1
was not protected. A future protection policy must cover every sector containing
immutable boot code.

### 4. UART depends on reset-default HSI

USART1 BRR is hard-coded for a 16 MHz peripheral clock. Any later clock-tree
change will break the baud rate unless the UART divider is derived from the
actual clock configuration.

### 5. Startup vector table is deliberately minimal

All device interrupts resolve to one default handler. This is acceptable for a
small polling bootloader, but not for the planned diagnostic application using
USB, DMA, timers, RTC and other peripherals.

### 6. Recovery is only an interface stub

`boot_mode_detect()` always returns normal mode and the recovery policy rejects
recovery requests. There is no physical button policy or update transport yet.

### 7. Signing private key is present in the archive

The repository archive includes `signing/exp016/firmware_signing_private.pem`.
This is acceptable only for an isolated laboratory key. It must never become a
production trust anchor or be committed to a shared repository.

### 8. Hardware probing should not be placed entirely in the immutable bootloader

The secure bootloader should remain small and deterministic. Destructive or
complex SDRAM/NAND/USB tests belong in a signed diagnostic application. The
bootloader should perform only the prerequisites required to authenticate and
launch that image.

## Schematic-to-software matrix

| Hardware | Schematic connection | Current state | Recommended owner |
|---|---|---|---|
| Four LEDs | PE3, PH10, PH11, PH12 | Implemented only on halt | Bootloader + diagnostic image |
| USART/CH340N | PA9/PA10 | Implemented, fixed HSI16 timing | Bootloader + diagnostic image |
| HSE | PH0/PH1, 25 MHz | Not used | Clock module |
| LSE | PC14/PC15, 32.768 kHz | Not used | Diagnostic image |
| SDRAM | FMC, W9825G6KH-6 | Not used | Diagnostic image |
| NAND | FMC, W29N01HVSINA | Not used | Diagnostic image |
| USB FS | PA11/PA12 and VBUS | Not used | Recovery/diagnostic image |
| Wake button | PA0 | Not used | Recovery policy/diagnostic image |
| User buttons | PC13, PE2, PI11 | Not used | Diagnostic image |
| SWD | PA13/PA14 | Used externally | Development only |
| RTC/VBAT | VBAT and LSE network | Not used | Diagnostic image |
| Expansion headers | Many GPIOs | Not testable without fixture | Optional loopback fixture |

## Recommended architecture

### Immutable bootloader

- reset-cause capture
- conservative clock baseline with timeout and fallback
- UART diagnostics
- physical recovery-request sampling
- signed-manifest bounds checks
- payload hash and signature verification
- version policy
- clean peripheral and interrupt handoff
- compact boot report in retained RAM

### Signed board-diagnostic application

- complete vector table and fault handlers
- HSE/PLL/LSE/RTC tests
- full internal SRAM tests using linker-defined regions
- SDRAM initialization and non-destructive/destructive test modes
- NAND ID, geometry, bad-block scan and reserved test block
- USB CDC diagnostic console
- USART console
- GPIO LEDs and buttons
- CRC, RNG, DMA, timers, ADC, IWDG and WWDG tests
- machine-readable test report

## Proposed implementation order

1. EXP060: commit this audit and freeze the hardware map.
2. EXP061: shared register definitions, board pin map and clock module.
3. EXP062: correct internal Flash/RAM memory model and application bounds.
4. EXP063: button-based recovery request and LED status codes.
5. EXP064: signed diagnostic application skeleton with full vector table.
6. EXP065: HSE/PLL/LSE/RTC tests.
7. EXP066: SDRAM driver and memory test.
8. EXP067: NAND identification and safe test policy.
9. EXP068: USB CDC diagnostics.
10. EXP069: peripheral self-test suite and final hardware coverage report.

## Erase/reset prerequisite

Do not erase the target until the new Flash layout, option-byte baseline and
first known-good bootloader/diagnostic images are built and archived. After that,
a full erase is appropriate because the existing target contents are explicitly
disposable.
