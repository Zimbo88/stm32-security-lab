# EXP066 Research Platform Core

EXP066 is a bounded Stage-1 application that can be linked for Slot A
(`0x08020200`) or Slot B (`0x08100200`) and signed either as a legacy signed
image or as an authenticated update package. It uses direct STM32F429 register
access, static buffers, and no heap. It is intended to be launched by verified
Stage 0.

Build:

```sh
make -C firmware/exp066_research_platform_core clean all
make -C firmware/exp066_research_platform_core signed \
  SIGNING_SEED=/path/to/development_or_release_seed.bin
make -C firmware/exp066_research_platform_core slot-releases \
  SIGNING_SEED=/path/to/development_or_release_seed.bin \
  PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
```

The signing seed must be supplied explicitly. No hardware flashing is performed
by the build.

The current implementation includes UART command dispatch, device identity,
curated read-only diagnostics, RAM-only experiment telemetry, a RAM log,
retained fault record, reset/fault health policy, non-blocking LED health
indication, bounded LED test commands, a health-gated application confirmation
service, and a module-manager placeholder. Register groups that could require
changing clock state are reported unavailable. Module execution, arbitrary
memory access, option-byte changes, and RDP activation are absent.

EXP071 adds health commands (`health status`, `health acknowledge`), LED status
and bounded test commands, and the bounded LED-only `easteregg knightrider`
command. Audio remains unavailable by default because no speaker or buzzer pin
is documented for the board.

Hardware procedure: connect USART1 (PA9/PA10, 115200 8-N-1), manually flash the
signed image only after independent review, reset, and exercise commands from
`docs/cli-reference.md`. Build and signing do not prove hardware behavior.

## Confirmation Gate

EXP066 does not confirm itself at reset. After the application reaches a stable
idle point, the confirmation service calls the existing Stage-0
`boot_confirm_current_slot` API only if early initialization completed, the
running slot was identified from VTOR, core self-checks passed, no critical
initialization failure was recorded, and metadata shows the running image is the
pending candidate or is already confirmed. The service attempts confirmation
once and reports the result through `confirmation status` and `telemetry show`.

## Cortex-M4 Vector Table

EXP066 is linked so `.isr_vector` starts at the selected slot payload base:
`0x08020200` for Slot A and `0x08100200` for Slot B. The core exception vectors
are ordered as:

| Index | Handler |
|---:|---|
| 0 | Initial MSP |
| 1 | `Reset_Handler` |
| 2 | `NMI_Handler` |
| 3 | `HardFault_Handler` |
| 4 | `MemManage_Handler` |
| 5 | `BusFault_Handler` |
| 6 | `UsageFault_Handler` |
| 7-10 | Reserved zero entries |
| 11 | `SVCall_Handler` |
| 12 | `DebugMon_Handler` |
| 13 | Reserved zero entry |
| 14 | `PendSV_Handler` |
| 15 | `SysTick_Handler` |

Weak default handlers are provided for exceptions that do not have a stronger
platform implementation. `HardFault_Handler` resolves to the retained fault
handler, not to the weak default.
