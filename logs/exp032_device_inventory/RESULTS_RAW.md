# EXP032 – STM32F429 Read-Only Inventory

## Debug connection

```text
Info : STLINK V2J37S7 (API v2) VID:PID 0483:3748
Info : Target voltage: 3.187377
Info : [stm32f4x.cpu] Cortex-M4 r0p1 processor detected
Info : [stm32f4x.cpu] target has 6 breakpoints, 4 watchpoints
```

## Register inventory

```text
SECTION|ARM_CORE_AND_SYSTEM_CONTROL
SCB_CPUID|0xE000ED00|0x410fc241
SCB_ICSR|0xE000ED04|0x3000
SCB_VTOR|0xE000ED08|0x0
SCB_AIRCR|0xE000ED0C|0xfa050000
SCB_SCR|0xE000ED10|0x0
SCB_CCR|0xE000ED14|0x200
SCB_SHCSR|0xE000ED24|0x0
SCB_CFSR|0xE000ED28|0x0
SCB_HFSR|0xE000ED2C|0x2
SCB_DFSR|0xE000ED30|0x8
SCB_MMFAR|0xE000ED34|0xe000edf8
SCB_BFAR|0xE000ED38|0xe000edf8
SCB_AFSR|0xE000ED3C|0x0
SECTION|DEVICE_IDENTIFICATION
DBGMCU_IDCODE|0xE0042000|0x20036419
```

## Safety

The target was accessed through read-only OpenOCD read_memory commands.

No flash programming, erase or option-byte write command was issued.
