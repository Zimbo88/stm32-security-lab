ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
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
}
