# EXP027A – RDP Level 1 Planning

## Current state

- MCU: STM32F429
- RDP: Level 0, option byte 0xAA
- FLASH_OPTCR: 0x0FFEAAED
- FLASH_OPTCR1: 0x0FFF0000
- SWD: available
- Bootloader sector 0: write protected
- Flash sectors 1 through 11: not write protected
- Application sector 2: writable
- Secure boot: operational
- Installed image version: 2
- Minimum accepted image version: 2

## Intended RDP1 behavior

After enabling RDP Level 1:

- External user-Flash readback through SWD must be blocked.
- Normal debugging of user Flash must be restricted.
- Boot from internal user Flash must continue.
- Secure boot must continue validating and starting the signed application.
- Sector-0 write protection should remain configured.

## Recovery consequence

Returning from RDP Level 1 to RDP Level 0 requires unlocking the device and
causes the internal user Flash to be erased.

Recovery therefore requires:

1. Return the device to RDP Level 0.
2. Restore the bootloader at 0x08000000.
3. Restore the signed version-2 image at 0x08008000.
4. Reapply sector-0 write protection.
5. Verify the option-register values.
6. Verify secure boot and application heartbeat over UART.

## Available recovery material

- Full 1 MiB Flash backup
- Sector-0 bootloader backup
- Sector-2 signed-image backup
- SHA-256 checksum files
- Recovery archive
- Standalone bootloader binary
- Standalone signed version-2 image

## Forbidden actions

- Never select RDP Level 2.
- Do not execute mass erase during EXP027A.
- Do not write a complete raw OPTCR value without decoding every field.
- Do not use an unverified RDP command.
- Do not proceed with unstable USB or power connections.
- Do not rely on WRP as a substitute for a verified recovery process.

## Go/no-go conditions for EXP027B

Proceed only if all conditions are true:

- Full Flash backup hashes pass.
- Recovery archive hash passes.
- Current option registers are documented.
- Sector-0 WRP is confirmed.
- Signed application boots successfully.
- The exact RDP1 command is independently verified.
- The exact RDP0 recovery command is independently verified.
- The expected mass-erase behavior is accepted.

## Go/no-go conditions for EXP027B

Proceed only if all conditions are true:

- Full Flash backup hashes pass.
- Recovery archive hash passes.
- Current option registers are documented.
- Sector-0 WRP is confirmed.
- Signed application boots successfully.
- The exact RDP1 command is independently verified.
- The exact RDP0 recovery command is independently verified.
- The expected mass-erase behavior is accepted.
