# Secure Boot HIL Implementation Plan

## Scope

Implement a Python 3.11 hardware-in-the-loop validation framework for the
existing STM32F429 secure-boot stack. The framework orchestrates builds,
immutable image caching, deterministic fault-image generation, `st-flash`
backup/write/read/reset commands, UART capture, assertions, restoration, and
reports. It does not modify firmware security logic or bypass any bootloader
checks.

## Repository Interfaces

- Bootloader build: `make -C firmware/exp045_bootloader_v2 clean all report
  LAYOUT_PROFILE=stm32f429_1m`
- EXP066 Slot A build: `make -C firmware/exp066_research_platform_core SLOT=a
  clean all verify-signed LAYOUT_PROFILE=stm32f429_1m ...`
- EXP066 Slot B build: same command with `SLOT=b`
- Slot package names: `exp066_research_platform_core_slot_a_update_v2.bin` and
  `exp066_research_platform_core_slot_b_update_v2.bin`
- Metadata provisioning helper: `tools/build/boot_metadata_provision.bin`
- Flash tool: `st-flash`
- UART banner framing:
  `STM32F429 SECURITY LAB` followed by `EXP045 BOOTLOADER V2`

## Transaction Strategy

Before destructive tests, back up bootloader, metadata A, metadata B, Slot A,
and Slot B. Every write is bounds-checked against configured flash regions.
Restoration is attempted on normal completion, test failure, subprocess
failure, UART failure, Python exception, `KeyboardInterrupt`, and SIGTERM. A
restore is considered successful only after readback bytes match original
backup hashes for every region.

## Build Cache Strategy

The application build directory is shared. The builder must build Slot A,
copy its package immediately into the run image cache, then build Slot B and
copy Slot B immediately. Later steps reference only cached immutable artifacts.

## Test Catalog Strategy

The initial catalog covers positive boots, authentication failures, payload
corruption, manifest erasure/zeroing, slot-policy observations, metadata
redundancy, reset stability, and performance extraction. Cases whose normative
metadata/slot policy is not fully specified are classified as `OBSERVE` rather
than `PASS`.

## Host Validation Strategy

Host tests use temporary repositories, fake process runners, and fake flash
transports. They do not require connected hardware. Tests cover configuration,
subprocess errors and timeouts, build cache ordering, immutable mutations,
UART framing, assertions, metrics, reporting, backup manifests, restore
idempotence, and suite exit-code rules.
