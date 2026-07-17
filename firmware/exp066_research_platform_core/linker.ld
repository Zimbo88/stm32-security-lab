ENTRY(Reset_Handler)
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08008200, LENGTH = 0x000F7E00
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
    .isr_vector : { . = ALIGN(256); KEEP(*(.isr_vector)) } > FLASH
    .text : { . = ALIGN(4); *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > FLASH
    .data : AT(_etext) { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM
    .bss (NOLOAD) : { . = ALIGN(8); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > RAM
    .noinit (NOLOAD) : { . = ALIGN(8); _snoinit = .; *(.noinit*) . = ALIGN(8); _enoinit = .; } > RAM
    _flash_image_end = LOADADDR(.data) + SIZEOF(.data);
    ASSERT(ADDR(.isr_vector) == 0x08008200, "vector table address changed")
    ASSERT(_flash_image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "platform exceeds Flash")
    ASSERT(_enoinit <= ORIGIN(RAM) + LENGTH(RAM), "platform exceeds SRAM")
    ASSERT((_estack & 7) == 0, "stack must be 8-byte aligned")
}
