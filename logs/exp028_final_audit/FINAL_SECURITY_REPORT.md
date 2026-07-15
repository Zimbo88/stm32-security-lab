# STM32F429 Security Lab – Final Security Report

## Platform

- MCU family: STM32F42x/F43x
- Device ID: 0x419
- Internal Flash: 1 MiB
- SRAM: 256 KiB
- Debug probe: ST-Link V2
- Communication: USART1 at 115200 baud

## Secure-boot memory layout

- Bootloader base: 0x08000000
- Bootloader Flash sector: sector 0
- Bootloader size: 12596 bytes
- Signed image base: 0x08008000
- Application vector table: 0x08008200
- Signed payload size: 996 bytes

## Cryptographic controls

- Payload hash: SHA-512
- Firmware signature: Ed25519
- Public key embedded in the bootloader
- Private signing key retained only on the host
- Signature verification occurs before application execution

## Image format

- Magic: 0x31474953
- Header version: 1
- Installed image version: 2
- Minimum accepted image version: 2
- Header size: 512 bytes
- Signature size: 64 bytes

## Verified security properties

### Integrity

A modified payload was rejected by the integrity check.

### Authenticity

A valid payload with a modified signed manifest was rejected because the
Ed25519 signature was invalid.

### Rollback prevention

Correctly signed image version 1 was rejected because the minimum accepted
version is 2.

Correctly signed image version 2 was accepted and executed.

### Bootloader write protection

Flash sector 0 is write protected.

Attempts to erase or overwrite the bootloader failed.

The application sector remained updateable.

### Readout protection

RDP Level 1 is active.

Normal internal boot remains functional.

External user-Flash readback did not disclose the original bootloader contents.

OpenOCD reported that the device security bit was set.

## Current device state

- Secure bootloader installed
- Signed application version 2 installed
- Sector-0 write protection active
- RDP Level 1 active
- Secure boot and application heartbeat operational

## Recovery material

The recovery package contains:

- Full 1 MiB Flash backup
- Sector-0 bootloader backup
- Sector-2 signed-image backup
- Standalone bootloader binary
- Standalone signed application image
- SHA-256 checksums
- Recovery documentation
- Compressed recovery archive

## Recovery warning

Returning from RDP Level 1 to RDP Level 0 causes the internal user Flash to be
mass-erased.

After unlocking, the bootloader and application must be restored and sector-0
write protection must be reapplied.

RDP Level 2 must never be selected in this laboratory.

## Security limitations

- The rollback minimum is compiled into the bootloader rather than stored in
  protected monotonic hardware state.
- Physical fault injection and invasive attacks were not evaluated.
- Side-channel resistance was not evaluated.
- Availability attacks remain possible.
- The UART output reveals firmware metadata and should be reduced or disabled
  in a production configuration.
- Key rotation and revocation are not implemented.
- Secure firmware transport and update authorization are outside the current
  implementation.
