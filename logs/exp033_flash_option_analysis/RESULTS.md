# EXP033 – Flash and Option-Byte Analysis

## Measured values

- FLASH_ACR: `0x00000000`
- FLASH_SR: `0x00000000`
- FLASH_CR: `0x80000000`
- FLASH_OPTCR: `0x0FFE00ED`
- FLASH_OPTCR1: `0x0FFF0000`

## Protection state

- RDP byte: `0x00`
- Interpreted RDP state: **Level 1**
- Bank 1 nWRP mask: `0xFFE`
- Bank 2 nWRP mask: `0xFFF`
- Protected Bank 1 sectors: `0`
- Protected Bank 2 sectors: `none`

## Flash controller

- Controller locked: `True`
- Busy flag: `False`
- Programming active: `False`
- Sector erase active: `False`
- Mass erase active: `False`

## Safety

This report was generated from previously recorded values.

No target connection, flash write, erase or option-byte change was performed.
