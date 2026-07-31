# Final WRP decision

## Scope

This decision is based on the `stm32f429_1m` layout and the read-only Option
Byte observation from 2026-07-31. No Option Byte write was performed.

Observed state:

- RDP: Level 0 (`OPTCR` RDP byte `0xAA`)
- WRP mask: `0xFFF`; no configured sector is write-protected
- Stage 0: sectors 0-1, `0x08000000..0x08008000`
- Boot metadata: sectors 2-3, `0x08008000..0x08010000`
- Update metadata: sector 4, `0x08010000..0x08020000`
- Slot A: sectors 5-7
- Slot B: sectors 8-10
- Recovery reserve: sector 11

## Evaluation

Stage 0 is outside the normal signed UART update target ranges and the
application MPU policy treats bootloader and metadata flash as read-only from
the application context. Those are software boundaries; they are not
physical flash protection.

WRP on sectors 0-1 would reduce accidental Stage-0 erasure, but it would also
make Stage-0 maintenance unavailable through the normal development path. It
does not protect metadata or slots, and it does not provide a recovery path for
a faulty Stage 0. WRP behavior must be checked against the exact STM32F429
revision and programmer implementation on an explicitly expendable device.

No expendable device was explicitly designated for a WRP experiment in this
campaign. The current board therefore remains unprotected and unchanged.

## Decision

**WRP RECOMMENDED BUT NOT YET HARDWARE VALIDATED**

Recommendation is limited to a later, separately approved experiment on a
clearly expendable chip. It is not a release or RDP2 prerequisite by itself.

Before any later WRP decision, record the exact sector mask, preserve a
verified baseline, validate the unprotected reference board, and confirm the
consequences for Stage-0 repair and RDP2. This repository contains no WRP
write command, Make target, CI step, or automatic provisioning path.
