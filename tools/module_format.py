"""Canonical EXP067 module package serializer and strict verifier.

The parser is intentionally pure and fail-closed.  It does not import firmware
code, does not execute payload bytes, and accepts only the fixed on-disk layout
documented in ``docs/module-format.md``.
"""

from __future__ import annotations

import hashlib
import hmac
import struct
import zlib
from collections.abc import Container, Iterable
from dataclasses import dataclass, replace

from nacl.signing import SigningKey, VerifyKey

MAGIC = b"MOD1"
FORMAT_VERSION = 1

TYPE_BYTECODE = 1
TYPE_NATIVE = 2
SUPPORTED_TYPES = frozenset({TYPE_BYTECODE, TYPE_NATIVE})

MAX_PACKAGE = 1024 * 1024
MAX_CAPABILITIES = 64
UINT32_MAX = 0xFFFFFFFF

HEADER_FORMAT = "<4sHH17I64s16sI64s16s"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
assert HEADER_SIZE == 240

CAPABILITY_FORMAT = "<I"
CAPABILITY_SIZE = struct.calcsize(CAPABILITY_FORMAT)
assert CAPABILITY_SIZE == 4


@dataclass(frozen=True)
class Header:
    magic: bytes
    format_version: int
    header_size: int
    total_size: int
    module_id: int
    module_version: int
    min_platform: int
    abi_version: int
    module_type: int
    flags: int
    code_size: int
    rodata_size: int
    data_size: int
    bss_size: int
    stack_size: int
    entry_offset: int
    capability_offset: int
    capability_size: int
    payload_offset: int
    payload_size: int
    payload_hash: bytes
    key_id: bytes
    header_crc32: int
    signature: bytes
    reserved: bytes


@dataclass(frozen=True)
class Manifest:
    header: Header
    capabilities: tuple[int, ...]

    @property
    def module_id(self) -> int:
        return self.header.module_id

    @property
    def module_version(self) -> int:
        return self.header.module_version


def _validate_u32(name: str, value: int, *, nonzero: bool = False) -> int:
    if not isinstance(value, int) or value < 0 or value > UINT32_MAX:
        raise ValueError(f"{name} must fit uint32")
    if nonzero and value == 0:
        raise ValueError(f"{name} must be nonzero")
    return value


def _unpack(blob: bytes) -> Header:
    return Header(*struct.unpack_from(HEADER_FORMAT, blob, 0))


def _pack_header(header: Header, *, signature_zero: bool, crc_zero: bool) -> bytes:
    return struct.pack(
        HEADER_FORMAT,
        header.magic,
        header.format_version,
        header.header_size,
        header.total_size,
        header.module_id,
        header.module_version,
        header.min_platform,
        header.abi_version,
        header.module_type,
        header.flags,
        header.code_size,
        header.rodata_size,
        header.data_size,
        header.bss_size,
        header.stack_size,
        header.entry_offset,
        header.capability_offset,
        header.capability_size,
        header.payload_offset,
        header.payload_size,
        header.payload_hash,
        header.key_id,
        0 if crc_zero else header.header_crc32,
        b"\0" * 64 if signature_zero else header.signature,
        header.reserved,
    )


def _header_for_crc(header: Header) -> bytes:
    return _pack_header(header, signature_zero=True, crc_zero=True)


def _header_for_signature(header: Header) -> bytes:
    return _pack_header(header, signature_zero=True, crc_zero=False)


def _region_end(name: str, offset: int, size: int, total: int) -> int:
    if offset < HEADER_SIZE:
        raise ValueError(f"invalid {name} range")
    if offset > total or size > total - offset:
        raise ValueError(f"invalid {name} range")
    return offset + size


def _decode_capabilities(
    blob: bytes,
    offset: int,
    size: int,
    supported_capabilities: Container[int] | None,
) -> tuple[int, ...]:
    if size % CAPABILITY_SIZE != 0:
        raise ValueError("capability alignment")

    count = size // CAPABILITY_SIZE
    if count > MAX_CAPABILITIES:
        raise ValueError("too many capabilities")

    caps = []
    seen = set()
    for relative in range(0, size, CAPABILITY_SIZE):
        cap = struct.unpack_from(CAPABILITY_FORMAT, blob, offset + relative)[0]
        if cap == 0:
            raise ValueError("invalid capability")
        if cap in seen:
            raise ValueError("duplicate capability")
        if supported_capabilities is not None and cap not in supported_capabilities:
            raise ValueError("unsupported capability")
        caps.append(cap)
        seen.add(cap)
    return tuple(caps)


def _encode_capabilities(
    capabilities: Iterable[int],
    supported_capabilities: Container[int] | None,
) -> tuple[tuple[int, ...], bytes]:
    caps = tuple(_validate_u32("capability", int(cap), nonzero=True) for cap in capabilities)
    if len(caps) > MAX_CAPABILITIES:
        raise ValueError("too many capabilities")
    if len(set(caps)) != len(caps):
        raise ValueError("duplicate capability")
    if supported_capabilities is not None:
        unsupported = [cap for cap in caps if cap not in supported_capabilities]
        if unsupported:
            raise ValueError("unsupported capability")
    return caps, b"".join(struct.pack(CAPABILITY_FORMAT, cap) for cap in caps)


def parse_package(
    blob: bytes,
    public_key: bytes | None = None,
    *,
    min_platform: int = 1,
    abi_version: int = 1,
    min_module_version: int = 0,
    supported_capabilities: Container[int] | None = None,
) -> Manifest:
    blob = bytes(blob)

    if len(blob) < HEADER_SIZE:
        raise ValueError("truncated header")
    if len(blob) > MAX_PACKAGE:
        raise ValueError("invalid total size")

    header = _unpack(blob)

    if (
        header.magic != MAGIC
        or header.format_version != FORMAT_VERSION
        or header.header_size != HEADER_SIZE
    ):
        raise ValueError("invalid header identity")
    if header.total_size != len(blob):
        raise ValueError("invalid total size")
    if (
        header.module_id == 0
        or header.module_version == 0
        or header.min_platform == 0
        or header.abi_version == 0
    ):
        raise ValueError("invalid version or module id")
    if header.module_type not in SUPPORTED_TYPES:
        raise ValueError("unsupported type")
    if header.flags != 0:
        raise ValueError("unsupported flags")
    if (
        header.reserved != b"\0" * 16
        or len(header.payload_hash) != 64
        or len(header.signature) != 64
        or len(header.key_id) != 16
    ):
        raise ValueError("reserved or key field")

    if header.min_platform > min_platform:
        raise ValueError("incompatible platform")
    if header.abi_version != abi_version:
        raise ValueError("incompatible abi")
    if header.module_version < min_module_version:
        raise ValueError("rollback version")

    capability_end = _region_end(
        "capability",
        header.capability_offset,
        header.capability_size,
        header.total_size,
    )
    payload_end = _region_end(
        "payload",
        header.payload_offset,
        header.payload_size,
        header.total_size,
    )

    if capability_end != header.payload_offset:
        raise ValueError("unexpected data between regions")
    if payload_end != header.total_size:
        raise ValueError("unexpected data after payload")
    if header.payload_offset < capability_end:
        raise ValueError("overlapping regions")
    if header.code_size > header.payload_size:
        raise ValueError("invalid code size")
    if header.entry_offset > header.code_size:
        raise ValueError("invalid entry offset")

    capabilities = _decode_capabilities(
        blob,
        header.capability_offset,
        header.capability_size,
        supported_capabilities,
    )
    payload = blob[header.payload_offset:payload_end]

    if not hmac.compare_digest(hashlib.sha512(payload).digest(), header.payload_hash):
        raise ValueError("payload hash")
    if zlib.crc32(_header_for_crc(header)) & UINT32_MAX != header.header_crc32:
        raise ValueError("header crc")

    if public_key is not None:
        public_key = bytes(public_key)
        if len(public_key) != 32:
            raise ValueError("public key length")
        if not hmac.compare_digest(hashlib.sha256(public_key).digest()[:16], header.key_id):
            raise ValueError("key id mismatch")
        signed = _header_for_signature(header) + blob[header.capability_offset:capability_end] + payload
        try:
            VerifyKey(public_key).verify(signed, header.signature)
        except Exception as exc:
            raise ValueError("invalid signature") from exc

    return Manifest(header, capabilities)


def build_package(
    payload: bytes,
    *,
    module_id: int,
    version: int,
    seed: bytes,
    capabilities: Iterable[int] = (),
    module_type: int = TYPE_BYTECODE,
    min_platform: int = 1,
    abi_version: int = 1,
    supported_capabilities: Container[int] | None = None,
) -> bytes:
    payload = bytes(payload)
    seed = bytes(seed)

    if len(seed) != 32:
        raise ValueError("Ed25519 seed must be exactly 32 bytes")
    _validate_u32("module_id", module_id, nonzero=True)
    _validate_u32("version", version, nonzero=True)
    _validate_u32("min_platform", min_platform, nonzero=True)
    _validate_u32("abi_version", abi_version, nonzero=True)
    if module_type not in SUPPORTED_TYPES:
        raise ValueError("unsupported type")

    _, capability_blob = _encode_capabilities(capabilities, supported_capabilities)
    payload_offset = HEADER_SIZE + len(capability_blob)
    total_size = payload_offset + len(payload)
    if total_size > MAX_PACKAGE:
        raise ValueError("package too large")

    signing_key = SigningKey(seed)
    public_key = bytes(signing_key.verify_key)
    header = Header(
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        total_size,
        module_id,
        version,
        min_platform,
        abi_version,
        module_type,
        0,
        len(payload),
        0,
        0,
        0,
        0,
        0,
        HEADER_SIZE,
        len(capability_blob),
        payload_offset,
        len(payload),
        hashlib.sha512(payload).digest(),
        hashlib.sha256(public_key).digest()[:16],
        0,
        b"\0" * 64,
        b"\0" * 16,
    )

    header = replace(header, header_crc32=zlib.crc32(_header_for_crc(header)) & UINT32_MAX)
    signed = _header_for_signature(header) + capability_blob + payload
    header = replace(header, signature=signing_key.sign(signed).signature)
    package = _pack_header(header, signature_zero=False, crc_zero=False) + capability_blob + payload

    parse_package(
        package,
        public_key,
        min_platform=min_platform,
        abi_version=abi_version,
        min_module_version=version,
        supported_capabilities=supported_capabilities,
    )
    return package
