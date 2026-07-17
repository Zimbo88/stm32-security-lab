# EXP066 Implementation Plan

EXP066 remains wire-compatible with the existing signed application envelope:
its vector table is linked at `0x08008200`, and the existing EXP065 signer can
wrap the resulting payload at `0x08008000` without changing Stage 0.

The implementation is split into bounded components: startup and fault entry,
direct-register board/UART/time support, retained fault records, a fixed RAM log,
curated snapshots, non-destructive tests, and a static CLI dispatcher. No API
accepts an address, function pointer, Flash destination, or arbitrary register.

Quality gates are a clean `-Wall -Wextra -Werror` build, linker map, explicit
Flash and RAM assertions, vector-address inspection, self-verifying signed image,
and host tests for pure parser/CRC behavior. Hardware validation is deferred and
must be performed manually over UART before EXP067 begins.

EXP067 should add a pure, fuzzable module-package parser and redundant catalog
without executing packages. It may reuse EXP066 services only after the public
ABI is documented; it must not change the Stage 0 envelope or Flash layout.
