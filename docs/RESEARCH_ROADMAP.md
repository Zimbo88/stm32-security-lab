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
