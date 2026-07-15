# EXP003 – USART1 baseline

Target: STM32F429IGT6, LQFP176

| Physical MCU pin | GPIO | Alternate function | Direction | Logic |
|---:|---|---|---|---|
| 143 | PA9 | AF7 / USART1_TX | STM32 output | 3.3 V TTL |
| 144 | PA10 | AF7 / USART1_RX | STM32 input | 3.3 V TTL |

Serial configuration: 115200 baud, 8 data bits, no parity, 1 stop bit.

Use an external USB-TTL adapter at **3.3 V logic**. Do not connect its VCC
output to the board. Cross TX and RX:

- STM32 PA9/TX -> adapter RXD
- STM32 PA10/RX <- adapter TXD
- GND -> GND

Remove the board's RX and TX routing jumpers while using the external adapter,
to prevent two transmitters from driving the same line. Keep CLK and DIO
jumpers fitted so the onboard ST-Link remains available.

## Build and flash

```bash
make clean
make
make flash
```

## Terminal

Replace `/dev/ttyUSB0` if necessary:

```bash
picocom -b 115200 --imap lfcrlf /dev/ttyUSB0
```
