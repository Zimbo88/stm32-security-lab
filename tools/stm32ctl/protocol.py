from __future__ import annotations

import binascii
import struct
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import Protocol

from .errors import CrcError, ProtocolError, ProtocolTimeout

MAGIC = b"SUPD"
VERSION = 1
HEADER_SIZE = 10
CRC_SIZE = 4
MAX_PAYLOAD_SIZE = 1024
MAX_FRAME_SIZE = HEADER_SIZE + MAX_PAYLOAD_SIZE + CRC_SIZE
WRITE_BLOCK_PREFIX_SIZE = 4
DEFAULT_MAX_WRITE_DATA_SIZE = MAX_PAYLOAD_SIZE - WRITE_BLOCK_PREFIX_SIZE

HEADER = struct.Struct("<4sBBHH")


class Command(IntEnum):
    HELLO = 0x01
    GET_INFO = 0x02
    BEGIN_UPDATE = 0x03
    WRITE_BLOCK = 0x04
    FINISH_UPDATE = 0x05
    GET_STATUS = 0x06
    ABORT_UPDATE = 0x07
    RESET = 0x08
    ACK = 0x80
    NACK = 0x81


class Status(IntEnum):
    OK = 0
    PARSER = 1
    STATE = 2
    FLASH = 3
    VERIFY = 4
    ROLLBACK = 5
    CRC = 6
    VERSION = 7
    LENGTH = 8
    OVERSIZE = 9
    SEQUENCE = 10
    UNKNOWN_COMMAND = 11
    TIMEOUT = 12
    IO = 13
    INVALID_ARGUMENT = 14
    INSTALLER = 15


class SessionState(IntEnum):
    IDLE = 0
    UPDATING = 1
    FINISHED = 2
    ABORTED = 3
    FAILED = 4
    RESET_REQUESTED = 5


class InstallerState(IntEnum):
    EMPTY = 0
    INITIALIZED = 1
    WRITING = 2
    PAYLOAD_COMPLETE = 3
    FINISHED = 4
    ABORTED = 5
    FAILED = 6


class ByteTransport(Protocol):
    def write(self, data: bytes) -> int | None:
        ...

    def read(self, size: int = 1) -> bytes:
        ...


@dataclass(frozen=True)
class Frame:
    version: int
    command: int
    sequence: int
    payload: bytes


@dataclass(frozen=True)
class Response:
    frame: Frame
    response_command: Command
    request_command: Command
    status: Status | int
    extra: bytes


@dataclass(frozen=True)
class Info:
    protocol_version: int
    max_payload_size: int
    max_write_data_size: int
    header_size: int
    crc_size: int
    session_state: int


@dataclass(frozen=True)
class TargetStatus:
    session_state: int
    last_status: Status | int
    expected_sequence: int
    installer_state: int
    accepted_payload_bytes: int
    image_version: int
    programmed_block_count: int


def status_name(value: Status | int) -> str:
    try:
        return Status(int(value)).name
    except ValueError:
        return f"UNKNOWN_STATUS_{int(value)}"


def command_name(value: Command | int) -> str:
    try:
        return Command(int(value)).name
    except ValueError:
        return f"UNKNOWN_COMMAND_{int(value):02X}"


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def encode_frame(command: int, sequence: int, payload: bytes = b"") -> bytes:
    if not 0 <= sequence <= 0xFFFF:
        raise ProtocolError(f"sequence out of range: {sequence}")
    if len(payload) > MAX_PAYLOAD_SIZE:
        raise ProtocolError(f"payload too large: {len(payload)} > {MAX_PAYLOAD_SIZE}")

    header = HEADER.pack(MAGIC, VERSION, int(command), sequence, len(payload))
    frame_crc = crc32(header + payload)
    return header + payload + struct.pack("<I", frame_crc)


def decode_frame(data: bytes) -> Frame:
    if len(data) < HEADER_SIZE + CRC_SIZE:
        raise ProtocolError("frame is truncated")

    magic, version, command, sequence, payload_length = HEADER.unpack(data[:HEADER_SIZE])
    if magic != MAGIC:
        raise ProtocolError("frame magic is invalid")
    if payload_length > MAX_PAYLOAD_SIZE:
        raise ProtocolError(f"frame payload too large: {payload_length}")

    expected_size = HEADER_SIZE + payload_length + CRC_SIZE
    if len(data) != expected_size:
        raise ProtocolError("frame length does not match payload length")

    payload = data[HEADER_SIZE : HEADER_SIZE + payload_length]
    expected_crc = struct.unpack_from("<I", data, HEADER_SIZE + payload_length)[0]
    actual_crc = crc32(data[: HEADER_SIZE + payload_length])
    if actual_crc != expected_crc:
        raise CrcError(f"bad CRC: expected 0x{expected_crc:08X}, calculated 0x{actual_crc:08X}")

    return Frame(version=version, command=command, sequence=sequence, payload=payload)


def _deadline(timeout: float) -> float:
    if timeout <= 0:
        raise ProtocolTimeout("timeout must be positive")
    return time.monotonic() + timeout


def _read_one(transport: ByteTransport, deadline: float) -> bytes:
    while time.monotonic() < deadline:
        chunk = transport.read(1)
        if chunk:
            return chunk[:1]
    raise ProtocolTimeout("timed out waiting for UART data")


def _read_exact(transport: ByteTransport, length: int, deadline: float) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        if time.monotonic() >= deadline:
            raise ProtocolTimeout("timed out waiting for complete frame")
        chunk = transport.read(length - len(chunks))
        if chunk:
            chunks.extend(chunk)
    return bytes(chunks)


def read_frame(transport: ByteTransport, timeout: float) -> Frame:
    deadline = _deadline(timeout)
    sync = bytearray()

    while True:
        sync.extend(_read_one(transport, deadline))
        if len(sync) > len(MAGIC):
            del sync[: len(sync) - len(MAGIC)]
        if bytes(sync) == MAGIC:
            break

    rest = _read_exact(transport, HEADER_SIZE - len(MAGIC), deadline)
    header = MAGIC + rest
    _, version, command, sequence, payload_length = HEADER.unpack(header)
    if payload_length > MAX_PAYLOAD_SIZE:
        raise ProtocolError(f"response payload too large: {payload_length}")

    payload_and_crc = _read_exact(transport, payload_length + CRC_SIZE, deadline)
    return decode_frame(header + payload_and_crc)


def write_frame(transport: ByteTransport, command: int, sequence: int, payload: bytes = b"") -> None:
    encoded = encode_frame(command, sequence, payload)
    written = transport.write(encoded)
    if written is not None and written != len(encoded):
        raise ProtocolError(f"short UART write: {written} of {len(encoded)} bytes")


def parse_response(frame: Frame, expected_sequence: int, request_command: Command) -> Response:
    if frame.version != VERSION:
        raise ProtocolError(f"unsupported response version: {frame.version}")
    if frame.sequence != expected_sequence:
        raise ProtocolError(
            f"unexpected response sequence: {frame.sequence}, expected {expected_sequence}"
        )
    if frame.command not in (Command.ACK, Command.NACK):
        raise ProtocolError(f"unexpected response command: {command_name(frame.command)}")
    if len(frame.payload) < 3:
        raise ProtocolError("response payload is truncated")

    response_request = frame.payload[0]
    if response_request != request_command:
        raise ProtocolError(
            f"response is for {command_name(response_request)}, expected {request_command.name}"
        )

    raw_status = struct.unpack_from("<H", frame.payload, 1)[0]
    try:
        status: Status | int = Status(raw_status)
    except ValueError:
        status = raw_status

    return Response(
        frame=frame,
        response_command=Command(frame.command),
        request_command=Command(response_request),
        status=status,
        extra=frame.payload[3:],
    )


def parse_info(extra: bytes) -> Info:
    if len(extra) != 10:
        raise ProtocolError(f"HELLO/GET_INFO payload has invalid size: {len(extra)}")
    return Info(
        protocol_version=extra[0],
        max_payload_size=struct.unpack_from("<H", extra, 1)[0],
        max_write_data_size=struct.unpack_from("<H", extra, 3)[0],
        header_size=struct.unpack_from("<H", extra, 5)[0],
        crc_size=struct.unpack_from("<H", extra, 7)[0],
        session_state=extra[9],
    )


def parse_status(extra: bytes) -> TargetStatus:
    if len(extra) != 18:
        raise ProtocolError(f"status payload has invalid size: {len(extra)}")
    raw_last_status = struct.unpack_from("<H", extra, 1)[0]
    try:
        last_status: Status | int = Status(raw_last_status)
    except ValueError:
        last_status = raw_last_status

    return TargetStatus(
        session_state=extra[0],
        last_status=last_status,
        expected_sequence=struct.unpack_from("<H", extra, 3)[0],
        installer_state=extra[5],
        accepted_payload_bytes=struct.unpack_from("<I", extra, 6)[0],
        image_version=struct.unpack_from("<I", extra, 10)[0],
        programmed_block_count=struct.unpack_from("<I", extra, 14)[0],
    )
