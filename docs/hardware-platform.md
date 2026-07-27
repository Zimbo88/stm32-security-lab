# Hardware Platform

## Current target

The current project targets an STM32F429 development board using an
STM32F429IGT6-class microcontroller.

<p align="center">
  <img src="assets/stm32f429-development-board.png"
       alt="STM32F429 development board"
       width="760">
</p>

## Core components

The minimum confirmed research setup consists of:

| Component | Purpose |
|---|---|
| STM32F429 MCU | Target executing the bootloader and signed applications |
| ST-LINK-compatible SWD probe | Flashing, reset control, memory access, and debugging |
| UART connection | Boot diagnostics and HIL result observation |
| Linux host | Build, signing, verification, test orchestration, and evidence capture |
| Stable USB connection | Probe and serial transport |
| Recoverable power source | Controlled reset and power-cycle experiments |

Board revisions may expose additional peripherals. Their presence should be
verified against the board schematic rather than inferred from similar
development boards.

## Validated Configuration

The current release evidence was collected on an STM32F429IGT6-class board at
RDP Level 0 with ST-LINK and USART1 attached. The secure-boot and dual-slot
secure-update path was validated with the generated `stm32f429_1m` layout
profile.

Confirmed board-local signals used by the current firmware:

| Signal | GPIO | Notes |
|---|---|---|
| USART1 TX | PA9 | 115200 baud, 8N1, 3.3 V TTL |
| USART1 RX | PA10 | 115200 baud, 8N1, 3.3 V TTL |
| LED1 | PE3 | Active-low |
| LED2 | PH10 | Active-low |
| LED3 | PH11 | Active-low |
| LED4 | PH12 | Active-low, normal EXP066 heartbeat |

No physical update or recovery GPIO has been selected. Do not invent one from a
similar board without repository-backed pinout evidence.

## Debug and serial interfaces

The HIL framework expects:

- an SWD connection through an ST-LINK-compatible probe;
- a serial device accessible to the development host;
- permission to open the serial device;
- stable target power during erase and write operations.

The serial device path is environment-specific and must not be hard-coded into
published documentation as a personal host path.

## Board schematic

Reference material is retained locally under `archive/` and is intentionally
excluded from version control unless redistribution rights are clear.

When publishing a schematic or board image, verify:

- copyright and redistribution permission;
- removal of personal desktop information;
- readable orientation;
- absence of serial numbers or other private identifiers;
- correspondence with the tested board revision.

## Hardware safety

Before running destructive or protection-related experiments:

1. capture a full recoverable baseline where possible;
2. verify bootloader and application binaries;
3. record option bytes and protection state;
4. confirm the exact MCU and flash size;
5. confirm recovery tooling;
6. avoid irreversible protection until recovery is no longer required.

RDP Level 2 may permanently disable normal debug access.

The current public release does not approve RDP1, RDP2, WRP, OTP, or Option
Byte provisioning. All hardware-validation evidence for the release was
collected with Option Bytes read only and left unchanged.

## ST-LINK reliability note

Repeated erase and write operations can occasionally fail because of USB,
probe, target-state, or flash-loader instability. A transient flashing failure
must be distinguished from a secure-boot rejection.

The HIL tooling reports command output and return codes so infrastructure
failures can be classified separately from firmware security results.
