# EXP033 Safety Record

EXP033 performs only offline decoding of register values previously captured
during EXP032.

The experiment does not contain OpenOCD commands.

The experiment does not connect to the STM32 target.

The experiment does not modify:

- FLASH_CR
- FLASH_OPTCR
- FLASH_OPTCR1
- RDP
- WRP
- PCROP
- flash memory

RDP Level 2 must not be enabled on the primary research board.
