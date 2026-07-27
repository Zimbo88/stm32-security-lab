# Release Readiness And Hardware Validation Status

## Current Status

The current release state is ready for public Open Source review as a
hardware-validated STM32F429 research reference at RDP Level 0.

It is not a certified production product and must not be used as an unreviewed
production boot chain.

## Validated On Real Hardware

Evidence was collected on an STM32F429IGT6-class board with ST-LINK and USART1
attached, using the `stm32f429_1m` layout profile and RDP Level 0.

Validated hardware behavior:

- EXP045 bootloader starts from `0x08000000`.
- Redundant metadata at `0x08008000` and `0x0800c000` is recovered.
- Slot A signed image at `0x08020000` boots after SHA-512 and Ed25519
  verification.
- Slot B signed image at `0x08080000` boots after authenticated update and
  trial selection.
- A-to-B update completed, booted Slot B as a trial candidate, and confirmed it.
- B-to-A update completed, booted Slot A as a trial candidate, and confirmed it.
- Same-version and lower-version updates were rejected by rollback policy.
- Corrupted manifest, target, signature, and payload cases were rejected
  fail-closed.
- Abort and reset during `WRITING` left only the confirmed fallback slot
  bootable.
- UART CRC, sequence, partial-frame, and random-byte negative cases did not
  block normal boot.
- Positive update readback matched the expected signed packages byte-for-byte.
- Option Bytes remained unchanged during the campaign.

## Host-Only Validation

Host tests provide repeatable evidence for:

- update package build, inspect, verify, and simulator paths;
- Ed25519/SHA-512 verifier boundary cases compiled from production C sources;
- boot metadata encode/decode/recovery and state transitions;
- update installer streaming, rollback, fault injection, and readback checks;
- UART parser framing, CRC, sequence, timeout, and resynchronization behavior;
- `stm32ctl` fake-serial update flow and error handling;
- diagnostic console command bounds and read-only behavior;
- EXP066 health, LED, RSM, confirmation, and telemetry logic;
- deterministic build comparison across exported source trees.

Host tests do not prove silicon reset behavior, physical power-loss timing,
Option Byte policy, WRP/RDP behavior, electrical fault response, or
side-channel resistance.

## Release-Quality Reference Components

The following components are suitable for external review and laboratory reuse
on the documented STM32F429 setup:

- EXP045 Stage-0 secure bootloader.
- EXP066 slot-linked research platform.
- Update-package tooling and offline verification reports.
- `stm32ctl` UART update client.
- HIL framework and repository audit scripts.
- Deterministic build and private-key scan tooling.

These components are still research reference code, not certified production
software.

## Experimental Components

The following areas are intentionally experimental or simulation-only:

- EXP067 module package tooling.
- EXP068 bytecode VM.
- EXP069 constrained native-module validator and simulator.
- EXP070 atomic module-installation simulator.
- EXP071 Runtime Security Monitor evidence collection beyond the validated
  boot/update path.
- Fault-injection, brownout, watchdog, option-byte, and RDP studies.

## Not Production Ready

The repository does not yet provide:

- production signing-key ceremony, custody, rotation, or revocation;
- hardware-backed monotonic rollback counters;
- WRP/RDP/Option-Byte provisioning workflow;
- RDP2 recovery evidence;
- authenticated operator identity for update sessions;
- physical recovery input;
- fleet-management or manufacturing provisioning tooling;
- third-party security certification;
- certified fault-injection, glitch, side-channel, or invasive-attack
  resistance.

## Release Recommendation

Recommended publication wording:

> Hardware-validated STM32F429 secure-boot and secure-update research
> reference at RDP Level 0. Not production-certified. RDP2 is not approved.

Do not describe the project as production-ready without a separate security
review, provisioning design, key-management process, and hardware protection
campaign.
