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
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
    .isr_vector : { . = ALIGN(256); KEEP(*(.isr_vector)) } > FLASH
    .text : { . = ALIGN(4); *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .ramfunc : AT(ADDR(.text) + SIZEOF(.text))
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
    .noinit (NOLOAD) : { . = ALIGN(8); _snoinit = .; *(.noinit*) . = ALIGN(8); _enoinit = .; } > RAM
    _flash_image_end = LOADADDR(.data) + SIZEOF(.data);
    ASSERT(ADDR(.isr_vector) == STM32F429_SELECTED_APPLICATION_BASE, "vector table address changed")
    ASSERT(_flash_image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "platform exceeds Flash")
    ASSERT(_enoinit <= ORIGIN(RAM) + LENGTH(RAM), "platform exceeds SRAM")
    ASSERT((_estack & 7) == 0, "stack must be 8-byte aligned")
}
