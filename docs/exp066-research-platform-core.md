# EXP066 Research Platform Core

EXP066 is a bounded Stage-1 application linked at `0x08008200` and signed by
the existing EXP065 envelope. It uses direct STM32F429 register access,
static buffers, and no heap. It is intended to be launched by verified Stage 0.

Build:

```sh
make -C firmware/exp066_research_platform_core clean all
make -C firmware/exp066_research_platform_core signed \
  SIGNING_SEED=/path/to/development_or_release_seed.bin
```

The signing seed must be supplied explicitly. No hardware flashing is performed
by the build.

The current implementation includes UART command dispatch, device identity,
curated read-only diagnostics, a RAM log, retained fault record, reset/fault
health policy, non-blocking LED health indication, bounded LED test commands,
and a module-manager placeholder. Register groups that could require changing
clock state are reported unavailable. Module execution, Flash installation,
arbitrary memory access, option-byte changes, and RDP activation are absent.

EXP071 adds health commands (`health status`, `health acknowledge`), LED status
and bounded test commands, and the bounded LED-only `easteregg knightrider`
command. Audio remains unavailable by default because no speaker or buzzer pin
is documented for the board.

Hardware procedure: connect USART1 (PA9/PA10, 115200 8-N-1), manually flash the
signed image only after independent review, reset, and exercise commands from
`docs/cli-reference.md`. Build and signing do not prove hardware behavior.

## Cortex-M4 Vector Table

EXP066 is linked so `.isr_vector` starts at `0x08008200`, the address expected
by EXP045. The core exception vectors are ordered as:

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
