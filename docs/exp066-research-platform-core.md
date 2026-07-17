# EXP066 Research Platform Core

EXP066 is a bounded Stage-1 application linked at `0x08008200` and signed by
the existing EXP065 envelope. It uses direct STM32F429 register access,
static buffers, and no heap. It is intended to be launched by verified Stage 0.

Build: `make -C firmware/exp066_research_platform_core clean all signed report`.
No hardware flashing is performed by the build.

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
