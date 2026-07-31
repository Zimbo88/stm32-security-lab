ENTRY(Reset_Handler)
INCLUDE ../common/stm32f429_memory_layout.ld
STM32F429_SELECTED_APPLICATION_BASE = DEFINED(STM32F429_SELECTED_APPLICATION_BASE) ?
    STM32F429_SELECTED_APPLICATION_BASE : STM32F429_APPLICATION_BASE;
STM32F429_SELECTED_APPLICATION_PAYLOAD_MAX_SIZE =
    DEFINED(STM32F429_SELECTED_APPLICATION_PAYLOAD_MAX_SIZE) ?
    STM32F429_SELECTED_APPLICATION_PAYLOAD_MAX_SIZE :
    STM32F429_APPLICATION_PAYLOAD_MAX_SIZE;
MEMORY
{
    FLASH (rx) : ORIGIN = STM32F429_SELECTED_APPLICATION_BASE, LENGTH = STM32F429_SELECTED_APPLICATION_PAYLOAD_MAX_SIZE
    RAM (rwx)  : ORIGIN = STM32F429_MAIN_SRAM_BASE, LENGTH = STM32F429_MAIN_SRAM_SUPPORTED_SIZE
}
STM32_APP_STACK_RESERVE_SIZE = 0x2000;
STM32_APP_STACK_GUARD_SIZE = 0x100;
_estack = ORIGIN(RAM) + LENGTH(RAM);
_stack_guard_start = _estack - STM32_APP_STACK_RESERVE_SIZE;
_stack_guard_end = _stack_guard_start + STM32_APP_STACK_GUARD_SIZE;
SECTIONS
{
    .isr_vector : { . = ALIGN(256); KEEP(*(.isr_vector)) } > FLASH
    .text : { . = ALIGN(4); *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .rdp2_marker :
    {
        . = ALIGN(4);
        __rdp2_slot_marker_start = .;
        KEEP(*(.rdp2_marker.slot))
        __rdp2_slot_marker_end = .;
    } > FLASH
    .ramfunc : AT(LOADADDR(.rdp2_marker) + SIZEOF(.rdp2_marker))
    {
        . = ALIGN(4);
        _sramfunc = .;
        *(.ramfunc*)
        . = ALIGN(4);
        _eramfunc = .;
    } > RAM
    _siramfunc = LOADADDR(.ramfunc);
    _etext = LOADADDR(.ramfunc) + SIZEOF(.ramfunc);
    .data : AT(_etext) { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM
    .bss (NOLOAD) : { . = ALIGN(8); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > RAM
    .rdp2_sram_marker (NOLOAD) :
    {
        . = ALIGN(4);
        __rdp2_sram_marker_start = .;
        KEEP(*(.noinit.rdp2_marker))
        __rdp2_sram_marker_end = .;
    } > RAM
    .noinit (NOLOAD) : { . = ALIGN(8); _snoinit = .; *(.noinit*) . = ALIGN(8); _enoinit = .; } > RAM
    _flash_image_end = LOADADDR(.data) + SIZEOF(.data);
    ASSERT(ADDR(.isr_vector) == STM32F429_SELECTED_APPLICATION_BASE, "vector table address changed")
    ASSERT(_flash_image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "platform exceeds Flash")
    ASSERT(STM32F429_LAYOUT_PROFILE_ID == STM32F429_LAYOUT_PROFILE_STM32F429_1M, "platform must use stm32f429_1m for hardware validation")
    ASSERT(STM32F429_FLASH_END == 0x08100000, "STM32F429IGT6 flash must end at 0x08100000")
    ASSERT(ORIGIN(FLASH) >= STM32F429_FLASH_BASE, "platform flash origin below internal flash")
    ASSERT(ORIGIN(FLASH) < STM32F429_FLASH_END, "platform flash origin outside 1 MiB flash")
    ASSERT(ORIGIN(FLASH) + LENGTH(FLASH) <= STM32F429_FLASH_END, "platform slot exceeds 1 MiB flash")
    ASSERT(STM32F429_SLOT_A_END <= STM32F429_SLOT_B_SIGNED_IMAGE_BASE, "Slot A and Slot B overlap")
    ASSERT(STM32F429_SLOT_B_END <= STM32F429_RECOVERY_BASE, "Slot B and recovery overlap")
    ASSERT(STM32F429_RECOVERY_END == STM32F429_FLASH_END, "recovery must end at 1 MiB flash end")
    ASSERT(_enoinit <= _stack_guard_start, "static RAM overlaps reserved stack")
    ASSERT((_stack_guard_start & (STM32_APP_STACK_GUARD_SIZE - 1)) == 0,
        "stack guard must be MPU aligned")
    ASSERT((_stack_guard_end <= _estack), "stack guard exceeds reserved stack")
    ASSERT((_estack & 7) == 0, "stack must be 8-byte aligned")
}
