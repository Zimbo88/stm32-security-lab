# EXP032 – System Memory Access Observation

## Observation

SWD attachment and reads from ARM CoreSight, SCB, DBGMCU and STM32 peripheral
registers succeeded.

A read of the factory flash-size register at address 0x1FFF7A22 failed during
the same debug session.

## Protection context

FLASH_OPTCR was read successfully.

The RDP byte is stored in FLASH_OPTCR bits 15:8.

The measured value is documented in RESULTS.md.

## Interpretation rule

The failed system-memory access is recorded as an observation under the
current protection state.

It must not be attributed solely to OpenOCD or solely to RDP until comparative
measurements under explicitly controlled protection states have been
performed.

## Safety

No option bytes were changed.

No protection level was changed.

No flash memory was erased or programmed.
