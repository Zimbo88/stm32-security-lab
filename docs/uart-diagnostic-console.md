# UART Diagnostic Console

The bootloader exposes a small read-only diagnostic console on the same UART
transport as the binary update protocol. It is intended for bounded startup
diagnostics, not for recovery writes or manual slot manipulation.

## Entry

After `board_clock_init()` and `uart_init()`, the bootloader opens the existing
bounded UART entry window.

- A complete valid binary `HELLO` frame with magic `SUPD`, protocol version 1,
  sequence 0, empty payload, and valid CRC enters binary update mode.
- A complete text line ending in CR or LF enters the diagnostic console.
- No input, malformed binary data, or noise that does not complete a text line
  or valid binary `HELLO` falls through to the normal secure boot sequence.
- The entry window remains poll-budget bounded, so UART noise cannot block boot
  forever.

The entry classifier emits no text while it is still deciding between binary
and text mode. Binary frames and console text are therefore not deliberately
mixed by the bootloader.

No physical update GPIO is selected yet because the concrete board pinout is
not documented unambiguously in the repository.

## Line Rules

- Maximum line length: 64 printable characters.
- Maximum token count: 4 tokens.
- Commands currently accept no arguments; additional tokens return `ERR args`.
- Space and tab separate tokens.
- CR or LF terminates a line.
- Backspace and DEL remove one buffered character.
- Other control bytes clear the partial line and return `ERR control`.
- Overlong lines are consumed until CR/LF and then return
  `ERR line-too-long`.
- Console inactivity returns `timeout` and exits to the normal boot path.
- There is no dynamic allocation.

## Commands

All commands are read-only except `boot` and `reboot`.

- `help`: print command list.
- `version`: print bootloader, board, layout, protocol, package, and minimum
  image version.
- `info`: print CPU clock and primary flash/application ranges.
- `slots`: print fixed slot descriptors from `boot_slot_lookup()`.
- `metadata`: recover and print boot metadata via
  `boot_metadata_recover_from_flash()`.
- `flashinfo`: print generated flash layout ranges.
- `resetcause`: print captured reset-cause flags without clearing them.
- `performance`: print the current performance counters.
- `status`: print console status and read-only indication.
- `boot`: leave the console and continue the normal secure boot sequence.
- `reboot`: request a controlled AIRCR/SYSRESETREQ reset.

The console deliberately has no erase, write, set-slot, set-version, RDP, or
Option Byte commands.
