# UART Binary Update Protocol

This document defines the deterministic binary protocol used by the secure
update transport layer. It is transport-neutral, but the intended bootloader
transport is USART1 on PA9/PA10 at 115200 baud.

## Frame Format

All multi-byte integer fields are little-endian. The CRC field is CRC32/IEEE
with reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR
`0xFFFFFFFF`, over the complete header and payload, excluding the CRC field
itself.

| Offset | Size | Field | Description |
| --- | ---: | --- | --- |
| 0 | 4 | Magic | ASCII `SUPD` |
| 4 | 1 | Protocol version | Current value: `1` |
| 5 | 1 | Command | Command or response code |
| 6 | 2 | Sequence number | Strictly increasing unsigned counter |
| 8 | 2 | Payload length | Number of payload bytes, max `1024` |
| 10 | N | Payload | Command-specific bytes |
| 10 + N | 4 | CRC32 | Little-endian CRC32/IEEE |

The maximum frame size is `10 + 1024 + 4 = 1038` bytes. Frames with a payload
length greater than 1024 are rejected before payload buffering.

## Commands

| Code | Name | Payload |
| ---: | --- | --- |
| `0x01` | `HELLO` | Empty |
| `0x02` | `GET_INFO` | Empty |
| `0x03` | `BEGIN_UPDATE` | Complete signed image header: manifest, signature, header padding |
| `0x04` | `WRITE_BLOCK` | `payload_offset:u32_le` followed by 1..1020 payload bytes |
| `0x05` | `FINISH_UPDATE` | Empty |
| `0x06` | `GET_STATUS` | Empty |
| `0x07` | `ABORT_UPDATE` | Empty |
| `0x08` | `RESET` | Empty |
| `0x80` | `ACK` | Response only |
| `0x81` | `NACK` | Response only |

Unknown command codes, including host-sent `ACK` and `NACK`, are rejected with
`UNKNOWN_COMMAND`.

## Responses

Responses are binary frames using the same header, sequence number and CRC
rules. The response sequence number equals the request sequence number.

Every response payload starts with:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 1 | Request command |
| 1 | 2 | Status code, little-endian |

`ACK` uses status `OK`. `NACK` uses a non-zero status.

`HELLO` and `GET_INFO` append:

| Offset | Size | Field |
| --- | ---: | --- |
| 3 | 1 | Protocol version |
| 4 | 2 | Maximum frame payload size |
| 6 | 2 | Maximum `WRITE_BLOCK` data bytes |
| 8 | 2 | Header size |
| 10 | 2 | CRC size |
| 12 | 1 | Protocol session state |

`GET_STATUS` appends the status block below. A successful `FINISH_UPDATE` ACK
also appends the same status block after `CANDIDATE_READY` has been committed.

| Offset | Size | Field |
| --- | ---: | --- |
| 3 | 1 | Protocol session state |
| 4 | 2 | Last protocol status |
| 6 | 2 | Next expected sequence number |
| 8 | 1 | Installer session state |
| 9 | 4 | Accepted update payload bytes |
| 13 | 4 | Installed image version from installer result |
| 17 | 4 | Programmed block count from installer result |

## Status Codes

| Code | Name | Meaning |
| ---: | --- | --- |
| `0` | `OK` | Request accepted |
| `1` | `PARSER` | Generic parser error |
| `2` | `STATE` | Command is not valid in the current protocol or installer state |
| `3` | `FLASH` | Flash, metadata, erase, program or readback failure |
| `4` | `VERIFY` | Package, signature, hash or installed-image verification failure |
| `5` | `ROLLBACK` | Image version is not newer than the confirmed version |
| `6` | `CRC` | CRC32 mismatch |
| `7` | `VERSION` | Unsupported protocol version |
| `8` | `LENGTH` | Command payload length is invalid |
| `9` | `OVERSIZE` | Frame payload length exceeds 1024 bytes |
| `10` | `SEQUENCE` | Frame sequence or update payload offset is not the next expected value |
| `11` | `UNKNOWN_COMMAND` | Command is not implemented |
| `12` | `TIMEOUT` | Input stopped in the middle of a known frame |
| `13` | `IO` | Transport output or reader error |
| `14` | `INVALID_ARGUMENT` | Local API misuse |
| `15` | `INSTALLER` | Installer fault not covered by a narrower status |

## Sequencing

The first host frame uses sequence `0`. Validly decoded frames with the expected
sequence number are executed exactly once and advance the expected sequence,
even if command execution returns `NACK`.

Frames with a bad CRC or oversized payload do not advance the expected
sequence. Frames with an unsupported protocol version also do not advance the
expected sequence. Duplicate and skipped sequence numbers are rejected with
`SEQUENCE` and leave the expected sequence unchanged.

The `WRITE_BLOCK` payload offset is a separate update-stream invariant. It must
match the number of payload bytes already accepted by the protocol session.
Skipped, repeated, overlapping or additional payload blocks are rejected.

## Parser Behavior

The parser is incremental and keeps a single fixed 1024-byte payload buffer in
the session. It does not allocate memory dynamically. Invalid bytes before the
magic value are discarded. Magic matching resynchronizes on subsequent `S`
bytes, so garbage before a valid frame does not poison the stream.

Both the initial entry window and later per-frame reader processing are bounded.
Continuous garbage cannot keep the bootloader in the update path indefinitely;
after the configured byte or poll budget is exhausted, the update path returns
to the normal boot decision unless a valid frame was completed.

The protocol session also contains the streaming installer session. Bootloader
integration should allocate the protocol session statically or globally, not as
a large automatic object on a constrained boot stack.

The command handler runs only after the parser has received the complete frame
and verified the CRC32. Payload length is checked before payload bytes are
buffered.

If a timeout occurs before a complete header was decoded, the parser is reset
and the API returns `TIMEOUT` without a command-specific NACK because no command
or sequence number is available yet. If the header was decoded, the bootloader
sends `NACK(TIMEOUT)` for that command and sequence.

## Secure Update Rules

The host never supplies a slot number, flash address or erase target in protocol
fields. `BEGIN_UPDATE` passes only the signed package header to the streaming
installer. The installer validates manifest fields, signature, target, image
type, inactive slot and rollback state before committing `WRITING` metadata or
erasing the candidate slot.

`WRITE_BLOCK` feeds only `update_installer_write()`. It carries an offset within
the signed payload, not a flash address. The bootloader continues to determine
the inactive candidate slot from confirmed metadata.

`FINISH_UPDATE` calls the streaming installer finish step. The installer keeps
the streaming SHA-512 state during transfer, verifies the completed payload
hash, reads the candidate image back from flash, verifies the installed package,
and only then commits `CANDIDATE_READY`.

Power loss or reset during begin, erase, write or finish leaves metadata in a
non-bootable state such as `WRITING` unless the installer has completed all
verification and committed `CANDIDATE_READY`.

## Text Output Separation

Binary update mode is treated as an exclusive transport mode by the bootloader
entry code. Human-readable boot diagnostics, diagnostic-console text and binary
protocol frames must not be emitted concurrently on the same UART stream. The
protocol module itself only emits binary ACK/NACK frames through its writer
callback.

The current bootloader integration enters this binary mode only when the
bounded UART entry window sees a valid `HELLO` frame with sequence number 0.
The same entry window can also enter the read-only diagnostic console when it
sees a complete text line; that console is documented separately in
`docs/uart-diagnostic-console.md`. There is intentionally no physical update
GPIO yet because the repository does not document a definitive board pin
assignment.
