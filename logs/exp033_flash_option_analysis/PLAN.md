# EXP033 – Flash and Option-Byte Analysis

## Objective

Decode the flash-controller and option-control register values captured during
EXP032.

## Input

The experiment uses the read-only OpenOCD register inventory from EXP032.

## Registers

- FLASH_ACR
- FLASH_SR
- FLASH_CR
- FLASH_OPTCR
- FLASH_OPTCR1

## Questions

- What RDP level is currently active?
- Which flash sectors are write-protected?
- Is a flash erase or programming operation active?
- Is the flash controller locked?
- Which option fields require additional mode-specific analysis?

## Method

This is an offline software analysis.

No target connection is opened.

## Safety

- No flash write
- No flash erase
- No option-byte write
- No RDP transition
- No RDP Level 2 operation
