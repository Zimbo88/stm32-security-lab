"""Pure redundant catalog record helpers for EXP067.

Records are fixed-size, append-only, and CRC-protected.  Recovery code can scan
a redundant catalog region and select the highest-sequence valid record without
trusting partially written or corrupted entries.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from typing import Iterable

RECORD_SIZE = 64
FINGERPRINT_SIZE = 16
UINT32_MAX = 0xFFFFFFFF

_BODY_FORMAT = "<9I16s"
_RECORD_FORMAT = "<9I16sI"
_BODY_SIZE = struct.calcsize(_BODY_FORMAT)
_RECORD_PREFIX_SIZE = struct.calcsize(_RECORD_FORMAT)
assert _BODY_SIZE == 52
assert _RECORD_PREFIX_SIZE == 56


@dataclass(frozen=True)
class CatalogRecord:
    sequence: int
    module_id: int
    version: int
    min_version: int
    state: int
    active: int
    pending: int
    failures: int
    result: int
    fingerprint: bytes

    def as_tuple(self) -> tuple[int, int, int, int, int, int, int, int, int, bytes]:
        return (
            self.sequence,
            self.module_id,
            self.version,
            self.min_version,
            self.state,
            self.active,
            self.pending,
            self.failures,
            self.result,
            self.fingerprint,
        )


def _u32(name: str, value: int, *, nonzero: bool = False) -> int:
    if not isinstance(value, int) or value < 0 or value > UINT32_MAX:
        raise ValueError(f"{name} must fit uint32")
    if nonzero and value == 0:
        raise ValueError(f"{name} must be nonzero")
    return value


def encode(
    sequence: int,
    module_id: int,
    version: int,
    min_version: int,
    state: int,
    active: int,
    pending: int,
    failures: int,
    result: int,
    fingerprint: bytes = b"",
) -> bytes:
    fingerprint = bytes(fingerprint)
    if len(fingerprint) > FINGERPRINT_SIZE:
        raise ValueError("fingerprint too large")
    if min_version > version:
        raise ValueError("minimum version exceeds current version")

    body = struct.pack(
        _BODY_FORMAT,
        _u32("sequence", sequence, nonzero=True),
        _u32("module_id", module_id, nonzero=True),
        _u32("version", version, nonzero=True),
        _u32("min_version", min_version),
        _u32("state", state),
        _u32("active", active),
        _u32("pending", pending),
        _u32("failures", failures),
        _u32("result", result),
        fingerprint.ljust(FINGERPRINT_SIZE, b"\0"),
    )
    crc = zlib.crc32(body) & UINT32_MAX
    prefix = body + struct.pack("<I", crc)
    return prefix + b"\0" * (RECORD_SIZE - len(prefix))


def decode_record(raw: bytes) -> CatalogRecord:
    raw = bytes(raw)
    if len(raw) != RECORD_SIZE:
        raise ValueError("record size")
    if raw[_RECORD_PREFIX_SIZE:] != b"\0" * (RECORD_SIZE - _RECORD_PREFIX_SIZE):
        raise ValueError("catalog reserved bytes")

    values = struct.unpack(_RECORD_FORMAT, raw[:_RECORD_PREFIX_SIZE])
    crc = values[-1]
    if zlib.crc32(raw[:_BODY_SIZE]) & UINT32_MAX != crc:
        raise ValueError("catalog crc")

    sequence, module_id, version, min_version = values[:4]
    if sequence == 0 or module_id == 0 or version == 0:
        raise ValueError("invalid catalog identity")
    if min_version > version:
        raise ValueError("invalid catalog version floor")

    return CatalogRecord(*values[:-1])


def decode(raw: bytes) -> tuple[int, int, int, int, int, int, int, int, int, bytes]:
    return decode_record(raw).as_tuple()


def iter_records(blob: bytes) -> Iterable[CatalogRecord]:
    blob = bytes(blob)
    if len(blob) % RECORD_SIZE != 0:
        raise ValueError("catalog blob size")
    for offset in range(0, len(blob), RECORD_SIZE):
        yield decode_record(blob[offset : offset + RECORD_SIZE])


def select_newest(records: Iterable[bytes]) -> CatalogRecord | None:
    newest: CatalogRecord | None = None
    for raw in records:
        try:
            record = decode_record(raw)
        except ValueError:
            continue
        if newest is None or record.sequence >= newest.sequence:
            newest = record
    return newest


def select_newest_from_blob(blob: bytes) -> CatalogRecord | None:
    blob = bytes(blob)
    if len(blob) % RECORD_SIZE != 0:
        raise ValueError("catalog blob size")
    return select_newest(blob[i : i + RECORD_SIZE] for i in range(0, len(blob), RECORD_SIZE))
