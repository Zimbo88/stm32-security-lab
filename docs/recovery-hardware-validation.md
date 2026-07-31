# Recovery hardware validation procedure

Status: PROCEDURE and log template. It is not a claim that all tests have
already been executed. This task explicitly does not perform power-loss,
RDP2, write-protection or option-byte experiments.

## Required inventory

Record MCU marking, board identifier, DBGMCU ID, unique-device fingerprint,
flash size, current RDP/option-byte state, ST-Link serial, UART device and
pinout. Save read-only identity output under the ignored local HIL results
directory. Do not commit dumps or device-specific secrets.

During ordinary operation the allowed path is power, GND, NRST, USART1 TX/RX,
the separate USB-UART adapter, `stm32ctl` and LED/UART output. ST-Link may be
used for the initial development flash and recovery of this pre-RDP lab board,
but is not used in the debugger-independent evidence runs.

## Test sequence

1. Provision a known signed Stage-0, Slot-A image and confirmed Slot-A metadata
   using the documented development procedure; record hashes and UART output.
2. Reset and capture `BOOT_RESET`, `Slot policy`, health and confirmation
   output (normal confirmed start).
3. Send a signed Slot-B package with `stm32ctl update`; verify `WRITING`,
   `CANDIDATE_READY`, trial boot, health gate and Slot-B confirmation.
4. Send a signed Slot-A package while B is confirmed; verify the inactive-slot
   rule, A trial boot and A confirmation.
5. Install `trial_no_confirm` and perform three controlled NRST resets. Record
   the persistent attempt count and automatic fallback to the confirmed slot.
6. Install `trial_watchdog_hang`; wait for the real IWDG reset and record
   `BOOT_RESET cause=IWDG`, metadata result and fallback.
7. Repeat with `trial_hardfault`, `trial_software_reset` and
   `trial_health_fail`. Do not use a debugger to recover between trials.
8. Send a package with a deliberately changed payload/signature and verify
   rejection before boot; verify the confirmed slot still starts.
9. Start an update and stop sending bytes without removing power. Verify
   `WRITING`/abort handling and the confirmed slot after NRST.
10. In an empty or metadata-unrecoverable simulator/board state, enter
    automatic recovery, run `stm32ctl info`, then upload a signed Slot-A
    bootstrap package and verify normal trial/confirmation. For an explicit
    request, send `recovery` to the Stage-0 text console during its entry
    window and verify the same signed-only behavior.

## Evidence record

For each run store the exact command, firmware/package hash, before/after slot
metadata, UART capture, reset cause, result and log path. Mark each row
`HOST TESTED`, `HARDWARE VALIDATED`, `DOCUMENTED ONLY` or `OPEN`; never infer
hardware validation from a successful host simulation.

## Explicit non-tests

No physical power interruption, deliberate flash corruption during erase or
program, fault injection, RDP/WRP option-byte write, or ROM bootloader test is
part of this procedure.
