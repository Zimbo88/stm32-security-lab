# Root-of-trust hardening report — Part 2

Date: 2026-07-31
Branch: `feature/root-of-trust-hardening`
Scope: Root of Trust, secure boot, key lifecycle, rollback, Stage-0 boundary,
MPU and memory hardening. RDP2 and WRP were not enabled.

## Ausgangszustand

The Part-1 parent contained EXP045 Stage 0, a single embedded Ed25519 public
key, Monocypher 4.0.3 Ed25519/SHA-512 verification, A/B slots, redundant
CRC/commit metadata, signed UART updates, trial confirmation, fallback,
software IWDG/reset-cause handling and signed UART recovery.

The device layout is Stage 0 `0x08000000..0x08008000`, metadata A/B
`0x08008000..0x08010000`, Slot A `0x08020000..0x08080000`, Slot B
`0x08080000..0x080E0000`, and recovery reserve `0x080E0000..0x08100000`.
The embedded public-key fingerprint is
`482dd9daac3d406f779995a50a00eb2ac9948eb412cba4403e2f81091780499a`.
There was no key ID/epoch/rotation path, no hardware monotonic counter, no
enforced application MPU policy, and no physical WRP. The previous software
rollback check compared a new package against `candidate_image_version` even
when that field described a previously rejected candidate.

## Implementierte Änderungen

- Added a five-region Cortex-M4 MPU policy: read-only Flash, XN SRAM,
  Stage-0/metadata read-only override, null no-access, and a 256-byte stack
  guard.
- Reserved an 8 KiB descending application stack budget and added linker
  assertions for static RAM and MPU alignment.
- Made MPU initialization a health-gate prerequisite. The existing metadata
  confirmation primitive is the only bounded exception and temporarily
  suspends/restores the MPU around its atomic commit.
- Added explicit `mpu_*` test scenarios without changing normal builds.
- Corrected rollback-floor selection: a candidate version in a pending or
  rejected state is not an installed floor. In `REJECTED_INVALID` and
  interrupted-candidate states, the signed confirmed active image is
  re-verified and supplies the floor.
- Added Stage-0 refresh points around the existing boot phases so an IWDG
  started by a prior application is serviced during bounded boot work.
- Added key lifecycle tooling with purpose labels, 0600 seed enforcement,
  atomic non-overwriting output, public fingerprint reporting and no-secret
  logging.
- Added package self-verification and the public-key fingerprint to build
  reports.
- Added RFC 8032 Ed25519 and SHA-512 known-answer tests.
- Added the security/threat model, trust chain, crypto audit, key-loss
  response, key-rotation ADR, Stage-0 boundary, WRP evaluation, MPU policy,
  stack budget and hardware evidence documentation.

## Architekturentscheidungen

### Schlüsselrotation

ADR-001 selects one embedded root public key and no in-field rotation. This is
the lower-risk choice for the fixed Stage-0 and metadata layout. A future
rotation design would need authenticated key epochs, atomic storage,
revocation/migration rules and power-loss evidence. No UART-triggered research
rollback mode or key-installation backdoor was added.

### Rollback-Policy

`MIN_IMAGE_VERSION` remains the compiled minimum. In `CONFIRMED`, the metadata
version is the confirmed software floor. In `WRITING`, `CANDIDATE_READY`,
`PENDING_TRIAL` and `REJECTED_INVALID`, the candidate version is not a floor;
the installer uses the signed manifest of the confirmed active slot. Equal and
lower versions are rejected before candidate erase. The STM32F429 has no
hardware-backed monotonic counter in this design.

### MPU and Stage 0

The MPU protects against accidental writes, null accesses, SRAM execution and
stack overrun. All application code remains privileged, so MPU is not
TrustZone and is not a complete boundary against a compromised application.
The UART installer already restricts writes to metadata A/B and the selected
inactive slot. WRP was evaluated only; no Option Byte was changed.

## Kryptografischer Audit

The target uses vendored Monocypher 4.0.3. The recorded source archive hash is
`a7cbae546fbdc489bca632c3747e1ceb8ca3d4bd39e2706a0916f28ccd280e50`; the
vendored crypto files have no local source delta. The verifier checks fixed
manifest fields, target slot, checked bounds, SHA-512, Ed25519, vector/MSP/reset
constraints and all relevant return values. It does not provide key rotation,
hardware anti-rollback or a hardware trust anchor. No external cryptographic
review or full fuzz campaign was performed.

## Hosttests

Executed commands and results:

```text
for d in tests/diagnostic_console tests/host_verifier tests/mpu_policy \
  tests/reset_cause tests/rsm_core tests/uart tests/update_protocol \
  tests/update_storage; do make -C "$d" clean test || exit 1; done  PASS
make -C tests/host_verifier clean test SANITIZE=1                       PASS
make -C tests/update_storage clean test SANITIZE=1                      PASS
python3 -m pytest -q                                                     PASS (175)
python3 tools/check_no_private_keys.py                                  PASS
python3 tools/rdp2_marker.py inspect --output /tmp/stm32-marker-report.json PASS (5 markers)
python3 tools/check_deterministic_build.py                              PASS
git diff --check                                                        PASS
```

The update-storage suite includes protected-range, parser, metadata,
rollback, rejected-candidate retry, streaming and failure-injection cases.
The new regression specifically proves that a rejected version 3 candidate
can be retried while version 2 remains rejected against confirmed version 2.
The six MPU test-firmware variants all built with `-Werror`; the normal
Slot-A/Slot-B artifacts and the Stage-0 artifact were rebuilt. The marker
tool resolved ELF symbols and matched the three flash markers plus the
configuration reference; the SRAM marker is runtime-initialized and therefore
has no binary match requirement.

## Hardwaretests

### Hardware record

- MCU family: STM32F42x/F43x, chip ID `0x419`, target STM32F429IGT6 profile.
- Debug ID: `0x20036419`.
- Flash/SRAM: 1024 KiB / 256 KiB.
- UART: USART1 through `/dev/ttyUSB0`, 115200 baud.
- ST-Link serial: `57FF6E067182525511261687`.
- Read-only `FLASH_OPTCR`: `0x0fffaaed`; RDP Level 0 (`0xAA`).
- Public-key fingerprint: `482dd9daac3d406f779995a50a00eb2ac9948eb412cba4403e2f81091780499a`.

The exact reversible control form used for development reset was:

```text
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c init -c "reset halt" -c resume -c shutdown
```

UART updates used the existing `Stm32Client` framing used by `stm32ctl`, with
512-byte blocks and the embedded public-key header. For the short normal-mode
boot window, the hardware harness synchronized the reversible OpenOCD reset
and the same client API; this is not claimed as a fully debugger-free run.
OpenOCD was used only for reversible development reset/restore in these runs;
no Option-Byte command was issued.

### Executed tests and observations

| Test | Firmware/package | Observed result | Evidence |
|---|---|---|---|
| Valid A→B | normal B, version 32 | `Slot decision = TRIAL`; `MPU policy=OK regions=5`; `WATCHDOG init=OK`; `HEALTH_GATE result=OK`; `SLOT_CONFIRMATION result=OK` | confirmed B metadata |
| Valid B→A | normal A, version 33 | same successful trial/health/confirmation sequence | confirmed A metadata |
| MPU normal path | normal B, version 32 | normal application reached health gate and confirmation | UART transcript |
| MPU fault/fallback | `TEST_SCENARIO=mpu_null_access`, B version 31 | `BOOT_RESET cause=IWDG`; trial B; after the bounded attempts `REJECTED_INVALID`, active A | UART plus metadata readback |
| False signature | B header signature bit changed | NACK `VERIFY` before erase | UART protocol response |
| Wrong key | package signed with temporary CI test key | NACK `VERIFY` before erase | UART protocol response |
| Manifest mutation | version field changed after signing | NACK `VERIFY` before erase | UART protocol response |
| Stage-0 vector target | vector changed to `0x08000000` after signing | NACK `VERIFY` before erase | UART protocol response |
| Equal-version rollback | valid B version 33 with confirmed A version 33 | NACK `ROLLBACK` before erase | UART protocol response |
| Final restore | valid B version 34 | accepted and confirmed after the normal trial path | metadata readback |

The MPU failure UART excerpt was:

```text
BOOT_RESET cause=IWDG raw=0x34000000
Slot decision    = TRIAL
MPU policy=OK regions=5 stack_guard=0x2001E000
WATCHDOG init=OK timeout_ms_nominal=4002
TEST_SCENARIO mpu_null_access
```

The fallback metadata snapshot after the failure campaign was
`state=REJECTED_INVALID`, `active_slot=A`, `candidate_slot=B`,
`candidate_image_version=31`. The later B32/A33/B34 updates returned the board
to a valid confirmed state. Additional reversible MPU scenario packages were
installed into Slot A at versions 41–45. The final board state was restored
with normal signed Slot B version 46.

The additional MPU hardware observations were:

| Scenario | Observed result | Evidence |
|---|---|---|
| `mpu_execute_sram` A41 | Trial started; invalid health path eventually fell back to B | UART and metadata readback |
| `mpu_write_bootloader` A42 | Trial started; IWDG reset `raw=0x24000000`; metadata later `REJECTED_INVALID` | UART and metadata readback |
| `mpu_write_metadata` A43 | Trial did not confirm; fallback selected | UART and metadata readback |
| `mpu_stack_guard` A44 | Trial started; IWDG reset `raw=0x24000000`; subsequent fallback | UART and metadata readback |
| `mpu_valid_application` A45 | `MPU policy=OK`, `HEALTH_GATE result=OK`, `SLOT_CONFIRMATION result=OK` | UART transcript |
| Normal restore B46 | `Slot decision = CONFIRMED`, health gate and confirmation succeeded | UART and metadata readback |

The local, uncommitted UART captures for these runs are under
`/tmp/stm32-root-trust-mpu-hardware/` and `/tmp/mpu_*.uart.log`.

One `st-flash --reset write ... 0x08000000` attempt failed in the SRAM flash
loader after erasing Stage-0 sectors. The known-good Stage-0 binary was then
restored and verified successfully with OpenOCD (`Verified OK`). This is a
real hardware warning: before RDP/WRP, SWD repair remained available; the
event must not be interpreted as debugger-free recovery evidence.

## Debuggerfreier Test

A complete debugger-free Part-2 campaign is not claimed. The negative update
authorization checks and package transfer used USART1 and `stm32ctl` framing,
but SWD/OpenOCD was used for reset timing and for recovering from the failed
development flash-loader attempt. A future run must use only power, NRST,
separate USB-UART and `stm32ctl`, and must record that reset was provided by
the board rather than SWD.

## Nicht getestete Punkte

- The complete key-loss/compromise ceremony and any key rotation are designed
  and documented only.
- Full stack worst-case call-graph proof and a complete static-analysis run
  remain open.
- The retained fault record was not independently read back for every MPU
  scenario; the hardware evidence is based on UART state, IWDG reset causes,
  fallback metadata and the valid-control scenario.
- Both metadata copies and both slots were not intentionally corrupted on
  hardware.
- A direct legacy `verify-signed` invocation against the current checkout was
  not run because no EXP065 signed artifact was present; the deterministic
  two-checkout run executed and passed the release-artifact verification path.
- No physical power-loss campaign, RDP2, WRP, fault injection, glitching,
  invasive analysis or external cryptographic audit was performed.
- No post-RDP2 repair or ROM-bootloader recovery was tested.

## Offene Risiken

- A Stage-0 defect remains a high-impact failure; post-RDP2 SWD/ROM repair is
  not assumed.
- Loss of the single private signing seed prevents trusted future updates.
- A copied signing seed can authorize arbitrary firmware; there is no in-field
  revocation or alternate key.
- Simultaneous loss of both slots or both metadata copies can require signed
  recovery or pre-RDP SWD repair.
- IWDG timing depends on LSI tolerance; the boot refresh points reduce, but do
  not eliminate, the dependency on bounded flash/crypto timing.
- MPU is not TrustZone and privileged application code can reconfigure it.
- WRP and RDP2 remain disabled, and the rollback source is software metadata,
  not a hardware monotonic counter.
- Physical power interruption and fault-injection behavior remain open.

## Bewertung von 0 bis 10

These ratings are evidence-based and deliberately do not exceed 9 without
complete implementation, automation, hardware validation and documentation.

| Area | Rating | Basis |
|---|---:|---|
| Root of Trust | 8 | explicit single-key chain; no hardware anchor |
| Secure Boot | 8 | signed boot chain and selected hardware evidence |
| Cryptographic validation | 8 | Monocypher provenance, vectors and negative tests |
| Key management | 7 | hardened tooling and lifecycle docs; no HSM |
| Key-loss strategy | 6 | honest irreversible boundary; no alternate key |
| Key rotation | 5 | deliberate non-implementation |
| Rollback protection | 8 | corrected confirmed-floor policy, host and hardware negative test; no hardware counter |
| Stage-0 protection | 8 | restricted writer and host boundary tests; WRP open |
| MPU | 7 | concrete policy, host descriptors, normal and null-fault hardware evidence |
| Stack/memory hardening | 6 | linker budget and guard; whole-path proof open |
| Update authorization | 8 | pre-erase negative hardware tests and readback/commit path |
| Hardware evidence | 7 | valid A/B, negative authorization and MPU fallback evidence; campaign incomplete |
| Documentation | 8 | models, ADR, audit, evidence and explicit limits |
| Open-source traceability | 8 | reproducible commands, source tests and no private material |

## Sicherheitsgrenze

RDP2 was not activated. Write Protection was not activated. Option Bytes were
not changed. No physical power-loss campaign was performed. No GitHub push and
no release were performed.

ROOT-OF-TRUST SOFTWARE COMPLETE – HARDWARE VALIDATION REMAINS
