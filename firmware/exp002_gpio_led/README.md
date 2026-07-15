# EXP002 – GPIO/LED baseline

Target: STM32F429IGT6  
Clock: reset-default HSI (~16 MHz)  
Outputs under test: PE3, PH10, PH11, PH12

This experiment does not modify option bytes and does not use external SDRAM,
NAND, USB, UART, interrupts, or the PLL.

## Build

```bash
make clean
make
```

## Inspect vector table

```bash
arm-none-eabi-objdump -s -j .isr_vector build/exp002_gpio_led.elf
arm-none-eabi-size build/exp002_gpio_led.elf
```

## Flash

```bash
make flash
```

## Restore the original erased state

The EXP001 baseline showed that all 1 MiB of internal Flash contained 0xFF.
To return to that state:

```bash
st-flash erase
```

Do not use any command that writes option bytes.
