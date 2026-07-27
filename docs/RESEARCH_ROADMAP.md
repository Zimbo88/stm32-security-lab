# STM32F429 Security Research Roadmap

## Project objective

This repository documents a controlled and reproducible investigation of the
STM32F429 architecture, peripherals, boot mechanisms and security functions.

The STM32F429IGT6 development board is the primary laboratory target. It is
owned, programmable and recoverable by the researcher.

The long-term objective is to understand the internally configured and
externally observable behavior of STM32F429 devices under different boot,
debug, flash-protection, reset, clock and communication configurations.

## Safety rule

RDP Level 2 must not be enabled on the primary STM32F429IGT6 development
board.

RDP Level 2 also must not be enabled for EXP071. The target-side secure-update
path has RDP0 hardware evidence, but physical recovery, irreversible
provisioning, hardware-backed rollback, host-side module-to-target
installation, and controlled power-loss behavior are not sufficient for RDP2.

Experiments involving irreversible configuration require:

- a separately designated expendable target
- a written risk assessment
- verified recovery limitations
- explicit confirmation immediately before execution

## Completed milestone

### v1.0.0 – Secure Boot

Completed work includes:

- bare-metal startup
- UART diagnostics
- custom bootloader
- CRC validation
- SHA-512 hashing
- Ed25519 image verification
- rollback protection
- flash write protection
- RDP Level 1
- recovery preparation
- boot timing measurement
- reset interruption campaign
- end-to-end secure boot validation

### v1.0.1 – Secure Update Hardware Validation

Completed work includes:

- streaming authenticated update installer;
- deterministic UART binary update protocol;
- `stm32ctl` host update client;
- Slot A to Slot B and Slot B to Slot A hardware update validation at RDP0;
- rollback rejection and corrupted-package negative tests on hardware;
- release-readiness documentation for public Open Source review.

### EXP066-EXP071 – Pre-hardware research platform integration

Completed repository-side work includes:

- signed EXP066 Stage-1 research-platform build;
- static allowlisted UART CLI;
- curated read-only diagnostics;
- RAM logging and retained fault record display;
- EXP067 signed package tooling;
- EXP068 host-side bytecode VM;
- EXP069 host-side constrained native-module validator/simulator;
- EXP070 host-side atomic module-installation simulator;
- EXP071 non-blocking platform LED health indication;
- EXP071 health/LED/Easter-egg CLI commands;
- EXP071 platform feature matrix and release-readiness documentation.

This is a pre-hardware integration milestone. Module execution, native-module
isolation, atomic module installation, update power-loss behavior, watchdog
recovery, and LED polarity/timing still require hardware validation before any
irreversible provisioning.

## Phase 2 – Architecture and security characterization

Planned experiments:

- EXP032: Device and board capability inventory
- EXP033: Debug port and CoreSight inventory
- EXP034: Reset source characterization
- EXP035: Clock source characterization
- EXP036: BOOT0 and boot-path matrix
- EXP037: STM32 system-memory bootloader characterization
- EXP038: Flash-controller register characterization
- EXP039: Option-byte lifecycle characterization
- EXP040: RDP Level 0 and Level 1 comparison
- EXP041: WRP and PCROP behavior
- EXP042: SRAM, CCM-RAM and backup-domain characterization
- EXP043: MPU and fault behavior
- EXP044: Independent and window watchdog behavior
- EXP045: Brownout and power-reset behavior
- EXP046: UART peripheral characterization
- EXP047: SPI peripheral characterization
- EXP048: I2C peripheral characterization
- EXP049: CAN peripheral characterization
- EXP050: USB peripheral characterization
- EXP051: DMA and bus-access characterization
- EXP052: FMC and external-memory interface characterization
- EXP053: RNG and cryptographic peripheral characterization
- EXP054: Interrupt and exception timing
- EXP055: Low-power and wake-up behavior
- EXP056: STM32F429IGT6 versus STM32F429VET6 comparison
- EXP057: Final laboratory characterization report

## Measurement policy

Every experiment should record:

- hardware configuration
- physical package pin number
- STM32 GPIO or dedicated pin name
- alternate function
- signal direction
- expected voltage
- wiring table
- firmware revision
- host-tool versions
- raw measurements
- interpreted results
- known limitations
- SHA-256 hashes of important evidence

## Repository policy

Raw measurements, firmware, scripts, reports and relevant hardware notes
remain versioned so that results can be independently reviewed and reproduced.
