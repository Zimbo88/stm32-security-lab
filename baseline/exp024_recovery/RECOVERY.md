# EXP024 Recovery Package

## Captured state

- MCU: STM32F429, device ID 0x419
- Internal Flash: 1 MiB
- Bootloader base: 0x08000000
- Bootloader size: 12596 bytes
- Signed image base: 0x08008000
- Signed image size: 1508 bytes
- Installed image version: 2
- Minimum accepted version: 2

## Current option state

- FLASH_OPTCR:  0x0FFFAAED
- FLASH_OPTCR1: 0x0FFF0000
- RDP Level 0
- No sector write protection

## Normal restoration

Restore the bootloader:

    st-flash --reset write \
      firmware/exp022_rollback_floor/build/exp022_rollback_floor.bin \
      0x08000000

Restore the signed application:

    st-flash --reset write \
      signing/exp022/signed_image_v2.bin \
      0x08008000

## Full-flash restoration

Only use this while no conflicting protection is active:

    st-flash --reset write \
      baseline/exp024_recovery/full_flash_1MiB.bin \
      0x08000000

## Warning

Do not modify RDP or write-protection option bytes without separately verifying
the exact command, target voltage, recovery method, and consequences.

Never select irreversible readout-protection settings in this laboratory.
