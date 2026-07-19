ENTRY(Reset_Handler)
INCLUDE ../common/stm32f429_memory_layout.ld

MEMORY
{
    FLASH (rx)  : ORIGIN = STM32F429_APPLICATION_BASE, LENGTH = STM32F429_APPLICATION_PAYLOAD_MAX_SIZE
    RAM   (rwx) : ORIGIN = STM32F429_MAIN_SRAM_BASE, LENGTH = STM32F429_MAIN_SRAM_SUPPORTED_SIZE
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data : AT(_etext)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    _flash_image_end = LOADADDR(.data) + SIZEOF(.data);

    ASSERT(ADDR(.isr_vector) == STM32F429_APPLICATION_BASE, "vector table address changed")
    ASSERT(_flash_image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "application exceeds Flash")
    ASSERT(STM32F429_LAYOUT_PROFILE_ID == STM32F429_LAYOUT_PROFILE_STM32F429_1M, "signed app must use stm32f429_1m for hardware validation")
    ASSERT(STM32F429_FLASH_END == 0x08100000, "STM32F429IGT6 flash must end at 0x08100000")
    ASSERT(ORIGIN(FLASH) >= STM32F429_FLASH_BASE, "application flash origin below internal flash")
    ASSERT(ORIGIN(FLASH) < STM32F429_FLASH_END, "application flash origin outside 1 MiB flash")
    ASSERT(ORIGIN(FLASH) + LENGTH(FLASH) <= STM32F429_FLASH_END, "application slot exceeds 1 MiB flash")
    ASSERT(_ebss <= ORIGIN(RAM) + LENGTH(RAM), "application exceeds SRAM")
    ASSERT((_estack & 7) == 0, "stack must be 8-byte aligned")
}
