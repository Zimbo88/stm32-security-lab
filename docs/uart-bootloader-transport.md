# Bootloader UART Transport

The bootloader uses USART1 in polling mode.

- TX: PA9, Alternate Function AF7.
- RX: PA10, Alternate Function AF7, internal pull-up enabled.
- Baud rate: 115200 baud.
- Clock basis: `uart_init()` uses `board_clock_get_sysclk_hz()`. With the
  current board-clock configuration the controller remains on 16 MHz HSI, which
  yields `USART1_BRR = 0x008B`.

## Receive Behavior

RX is intentionally interrupt-free and DMA-free. Timeouts are poll budgets, not
calibrated wall-clock milliseconds:

- `uart_getc_nonblocking()` checks once for one byte.
- `uart_getc_timeout()` polls at most `timeout_polls` times.
- `uart_read_timeout()` applies the poll budget to each expected byte and
  returns the number of bytes already read.
- `uart_flush_rx()` discards pending RX data and stops after a fixed drain
  limit.

USART errors are detected before a byte is handed to the caller. Overrun,
framing, noise, and parity errors are cleared through the STM32F4 sequence of
reading the status register and then the data register. The affected byte is
discarded and the error status is returned to the caller.

## Transport-Neutral Reader Layer

`byte_reader_t` wraps a nonblocking byte reader with a context pointer. Parsers
use `byte_reader_getc_timeout()` and `byte_reader_read_timeout()` instead of
direct USART register access. The target initializes this with
`uart_byte_reader_init()`. Host tests use fake readers without USART registers.

## Bootloader Entry Window

After `board_clock_init()` and `uart_init()`, the bootloader opens a short,
bounded UART entry window:

- A valid binary `HELLO` frame with protocol sequence number 0 activates update
  mode.
- A complete text line activates the read-only diagnostic console.
- No valid `HELLO`, no complete text line, UART noise, or poll-budget expiry
  continues into normal secure boot.

The entry window has:

- no DMA dependency;
- no interrupt dependency;
- no ST-ROM-bootloader dependency;
- no Option-Byte or RDP change;
- no physical update GPIO.

No update GPIO is selected yet because the repository does not document a
definitive spare board pin for that purpose. Until then, update and diagnostic
mode are entered only through the bounded UART entry window.

In update mode, the bootloader does not emit human-readable diagnostic lines on
the same UART stream. Responses are binary ACK/NACK frames only. After a
successful `FINISH_UPDATE`, the installer must have committed
`CANDIDATE_READY`; the bootloader then waits for UART transmission complete and
requests a controlled AIRCR/SYSRESETREQ reset.

The text console is documented in `docs/uart-diagnostic-console.md`. It has no
Flash write, erase, slot, version, Option-Byte, or RDP commands.
