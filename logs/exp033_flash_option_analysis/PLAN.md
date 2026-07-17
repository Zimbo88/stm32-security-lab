# EXP033 – Flash and Option-Byte Analysis

## Objective

Decode the flash-controller and option-byte register values captured during
EXP032.

## Input

- FLASH_ACR
- FLASH_SR
- FLASH_CR
- FLASH_OPTCR
- FLASH_OPTCR1

## Method

Offline analysis of previously captured register values.

## Result summary

- RDP byte: 0x00
- Interpreted RDP state: Level 1
- Bank 1 write-protected sectors: sector 0
- Bank 2 write-protected sectors: none
- Flash controller locked
- No flash operation active

## Safety

No connection to the target was opened during EXP033.

No flash memory, register or option byte was modified.
