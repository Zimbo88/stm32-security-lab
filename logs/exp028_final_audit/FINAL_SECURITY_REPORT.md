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

A modified application payload was rejected. The bootloader did not transfer
control to the application.

### Authenticity

A valid payload with a modified signed manifest was rejected because the
Ed25519 signature was invalid.

### Rollback prevention

A correctly signed image with version 1 was rejected because the minimum
accepted version was 2.

A correctly signed image with version 2 was accepted and executed.

### Bootloader write protection

Flash sector 0 is write protected.

An attempted erase or overwrite of the bootloader failed. A subsequent
readback comparison confirmed that the bootloader remained unchanged.

The application sector remained writable before RDP Level 1 was enabled.

### Readout protection

RDP Level 1 is active.

Normal internal boot remains functional. External user-Flash readback did not
disclose the original bootloader data.

OpenOCD reported:

    Device Security Bit Set

Flash-size detection by external tools became unreliable under protection,
which is consistent with inaccessible protected device information.

## Current device state

- Secure bootloader installed at 0x08000000
- Signed application version 2 installed at 0x08008000
- Application vector table at 0x08008200
- Sector-0 write protection active
- RDP Level 1 active
- SHA-512 and Ed25519 verification operational
- Rollback minimum version set to 2
- Application heartbeat operational

## Recovery material

The EXP024 recovery package contains:

- Full 1 MiB Flash backup
- Sector-0 bootloader backup
- Sector-2 signed-image backup
- Standalone bootloader binary
- Standalone signed application image
- SHA-256 checksums
- Recovery documentation
- Compressed recovery archive

All recorded recovery hashes passed during the final audit.

## Recovery warning

Returning from RDP Level 1 to RDP Level 0 causes the internal user Flash to be
mass-erased.

After unlocking:

1. Restore the bootloader at 0x08000000.
2. Restore the signed image at 0x08008000.
3. Reapply sector-0 write protection.
4. Verify all option bytes.
5. Verify secure boot over UART.

RDP Level 2 must never be selected in this laboratory.

## Private-key handling

The firmware signing private key exists locally on the development host but is
excluded by `.gitignore`.

The final Git audit found no tracked filename matching the private signing key,
a `.key` suffix, or the configured private-key naming pattern.

## Security limitations

- The rollback minimum is compiled into the bootloader rather than stored in
  protected monotonic hardware state.
- Physical fault injection was not evaluated.
- Invasive attacks were not evaluated.
- Side-channel resistance was not evaluated.
- Availability attacks remain possible.
- UART output exposes firmware metadata and should be disabled or restricted
  in a production configuration.
- Key rotation and revocation are not implemented.
- Secure firmware transport is outside the current implementation.
- Update authorization beyond signature verification is not implemented.
- RDP Level 1 is reversible only through a destructive return to Level 0.

## Final assessment

The laboratory implementation demonstrates a functioning STM32F429 secure
boot chain combining:

- SHA-512 payload integrity
- Ed25519 firmware authenticity
- signed-image version enforcement
- bootloader sector write protection
- RDP Level 1 readout protection
- documented recovery material

The installed signed version-2 application boots successfully while invalid,
modified, and rollback images are rejected.
