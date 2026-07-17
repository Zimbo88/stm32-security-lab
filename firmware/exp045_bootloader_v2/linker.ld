ENTRY(Reset_Handler)
INCLUDE ../common/stm32f429_memory_layout.ld

MEMORY
{
    FLASH (rx)  : ORIGIN = STM32F429_BOOTLOADER_BASE, LENGTH = STM32F429_BOOTLOADER_SIZE
    RAM   (rwx) : ORIGIN = STM32F429_MAIN_SRAM_BASE, LENGTH = STM32F429_MAIN_SRAM_SUPPORTED_SIZE
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

_flash_region_start = ORIGIN(FLASH);
_flash_region_end = ORIGIN(FLASH) + LENGTH(FLASH);

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
        . = ALIGN(8);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM

    /*
     * Include the initial values of .data because they are stored in flash
     * even though the section executes from RAM.
     */
    _flash_image_end = LOADADDR(.data) + SIZEOF(.data);

    ASSERT(
        _flash_image_end <= _flash_region_end,
        "ERROR: bootloader flash image exceeds reserved 32 KiB region"
    )

    ASSERT(
        _flash_region_end == STM32F429_BOOTLOADER_END,
        "ERROR: bootloader linker region diverges from target memory layout"
    )
}
