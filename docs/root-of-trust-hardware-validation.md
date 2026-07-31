# Root-of-trust hardware validation plan and evidence

This document separates actions run from actions only prepared. The target
remained RDP0 with unchanged Option Bytes for Part 2.

## Board record

The recorded development target was an STM32F429IGT6-class STM32F42x/F43x
device with chip ID `0x419`, DBGMCU ID `0x20036419`, 1024 KiB Flash, and 256
KiB SRAM. USART1 was `/dev/ttyUSB0` at 115200 baud. The ST-Link was
`<redacted>`. A read-only `FLASH_OPTCR` observation was
`0x0fffaaed`, corresponding to RDP Level 0; no Option-Byte write was issued.
The embedded public-key fingerprint was
`482dd9daac3d406f779995a50a00eb2ac9948eb412cba4403e2f81091780499a`.

The raw shell transcripts are local lab evidence and are intentionally not
committed. The commands below are the exact test forms used. The normal-mode
boot-window runs used the `Stm32Client` framing code also used by `stm32ctl`;
the harness synchronized reset timing with OpenOCD, so they are not
debugger-free evidence.

## Executed evidence

| Test | Command/result | Evidence |
|---|---|---|
| Bootloader build | `make -C firmware/exp045_bootloader_v2 all` — PASS, 30,916 bytes / 32,768 | size check |
| Valid A→B | signed UART package version 32, then reset | `Slot decision = TRIAL`, `MPU policy=OK`, `WATCHDOG init=OK`, `HEALTH_GATE result=OK`, `SLOT_CONFIRMATION result=OK`; metadata confirmed B |
| Valid B→A | signed UART package version 33, then reset | same health/confirmation output; metadata confirmed A |
| MPU null access | signed B version 31 retried after rejection | `BOOT_RESET cause=IWDG`, trial B, `MPU policy=OK`; after the bounded attempts metadata was `REJECTED_INVALID`, active A |
| Wrong signature | malformed B header sent by UART | target NACK `VERIFY`; no erase/commit |
| Wrong key | package signed by a temporary CI test key | target NACK `VERIFY`; no erase/commit |
| Manifest mutation | signed package header field changed | target NACK `VERIFY`; no erase/commit |
| Stage-0 vector target | package vector changed to `0x08000000` | target NACK `VERIFY`; no erase/commit |
| Same-version rollback | valid B package version 33 while A version 33 confirmed | target NACK `ROLLBACK`; no erase/commit |
| `mpu_execute_sram` | signed A version 41, trial fault path | trial remained unconfirmed and later fell back to B |
| `mpu_write_bootloader` | signed A version 42, CPU write access only | IWDG reset `raw=0x24000000`, then `REJECTED_INVALID` fallback |
| `mpu_write_metadata` | signed A version 43, CPU write access only | unconfirmed trial and fallback |
| `mpu_stack_guard` | signed A version 44, guard access only | IWDG reset `raw=0x24000000`, then fallback |
| `mpu_valid_application` | signed A version 45 | `MPU policy=OK`, health gate and confirmation succeeded |
| Normal restore | signed B version 46 | normal B confirmed; final metadata copy selected B |

The board was finally restored with a valid newer B package. A transient read
during its confirmation showed the normal journal sequence (`WRITING`/trial
copy plus the previous confirmed copy); after confirmation the journal again
contained a valid confirmed record.

One `st-flash --reset write ... 0x08000000` attempt failed in its SRAM flash
loader after erasing Stage-0 sectors. The known-good Stage-0 binary was
successfully restored and verified with OpenOCD. This is evidence for the
documented SWD-before-RDP recovery risk, not a success claim for debugger-free
repair.

Record MCU/board identity, DBGMCU ID, flash size, UID fingerprint, USART1
device, ST-Link serial, RDP level, Option Bytes, current metadata, key
fingerprint, firmware hash, and a dated log before each run. Use the signed
UART path for slot tests where possible. SWD is allowed for restoring a known
development baseline, never for changing RDP/WRP.

Run valid boot, wrong signature, wrong key, manifest mutation, rollback, equal
version, slot-boundary, Stage-0-boundary, normal MPU, and each `mpu_*`
scenario. For faults, capture UART output, retained CFSR/HFSR, reset cause, and
confirmed-slot behavior. Do not perform a write test that could erase or
program a protected region; the MPU scenarios issue CPU accesses only and must
be abandoned if the policy is not enabled.

Software and host descriptors are implemented and host tested. The normal
boot/update path, selected negative authorization cases, and the complete
`mpu_*` scenario matrix are hardware validated on this board for the tested
fallback/control behavior. The retained fault record was not independently
read back for every scenario. An independent UART-only recovery run and
complete production provisioning evidence remain open. No RDP2 or WRP
validation claim is made.
