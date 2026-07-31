from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

import pytest
from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from stm32ctl.cli import DEFAULT_TIMEOUT  # noqa: E402
from stm32ctl.client import DEFAULT_RESPONSE_TIMEOUT_SECONDS, Stm32Client  # noqa: E402
from stm32ctl.errors import CrcError, NackError, ProtocolTimeout  # noqa: E402
from stm32ctl.package import load_and_verify_package  # noqa: E402
from stm32ctl.protocol import (  # noqa: E402
    HEADER_SIZE,
    Command,
    Status,
    decode_frame,
    encode_frame,
)
from stm32f429_layout import LAYOUT  # noqa: E402

SIGNER_PATH = ROOT / "firmware" / "exp065_signed_app" / "tools" / "build_signed_image.py"
SEED = bytes(range(32))
PUBLIC_KEY_HEX = bytes(SigningKey(SEED).verify_key).hex()


def load_signer():
    spec = importlib.util.spec_from_file_location("stm32ctl_test_signer", SIGNER_PATH)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def make_application(slot: str, size: int = 1536) -> bytes:
    image = bytearray(b"\xA5" * size)
    vector_base = LAYOUT[f"slot_{slot}_payload_base"]
    struct.pack_into("<II", image, 0, LAYOUT["application_msp_end"], vector_base | 1)
    return bytes(image)


def make_package(tmp_path: Path, slot: str = "b", size: int = 1536):
    signer = load_signer()
    package, _, _, _ = signer.build_update_package(
        make_application(slot, size),
        SEED,
        slot=slot,
        image_version=3,
    )
    package_path = tmp_path / "firmware.update.bin"
    package_path.write_bytes(package)
    return load_and_verify_package(package_path, public_key_hex=PUBLIC_KEY_HEX)


def response_frame(
    request: Command,
    sequence: int,
    *,
    status: Status = Status.OK,
    response: Command = Command.ACK,
    extra: bytes = b"",
) -> bytes:
    payload = bytes([request]) + struct.pack("<H", int(status)) + extra
    return encode_frame(response, sequence, payload)


def info_extra() -> bytes:
    return struct.pack("<BHHHHB", 1, 1024, 1020, HEADER_SIZE, 4, 0)


def status_extra(
    *,
    session_state: int = 0,
    last_status: Status = Status.OK,
    expected_sequence: int = 0,
    installer_state: int = 0,
    accepted_payload: int = 0,
    image_version: int = 0,
    blocks: int = 0,
) -> bytes:
    return struct.pack(
        "<BHHBIII",
        session_state,
        int(last_status),
        expected_sequence,
        installer_state,
        accepted_payload,
        image_version,
        blocks,
    )


class ScriptedSerial:
    def __init__(self, responses: list[bytes] | None = None) -> None:
        self.responses = list(responses or [])
        self.read_buffer = bytearray()
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        if self.responses:
            self.read_buffer.extend(self.responses.pop(0))
        return len(data)

    def read(self, size: int = 1) -> bytes:
        if not self.read_buffer:
            return b""
        chunk = bytes(self.read_buffer[:size])
        del self.read_buffer[:size]
        return chunk


class FakeTargetSerial:
    def __init__(
        self,
        *,
        drop_once: set[Command] | None = None,
        nack_once: dict[Command, Status] | None = None,
    ) -> None:
        self.read_buffer = bytearray()
        self.writes: list[bytes] = []
        self.commands: list[Command] = []
        self.expected_sequence = 0
        self.update_offset = 0
        self.blocks = 0
        self.drop_once = set(drop_once or set())
        self.nack_once = dict(nack_once or {})

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        frame = decode_frame(data)
        command = Command(frame.command)
        self.commands.append(command)
        response = self._handle(command, frame.sequence, frame.payload)
        if response:
            self.read_buffer.extend(response)
        return len(data)

    def read(self, size: int = 1) -> bytes:
        if not self.read_buffer:
            return b""
        chunk = bytes(self.read_buffer[:size])
        del self.read_buffer[:size]
        return chunk

    def _handle(self, command: Command, sequence: int, payload: bytes) -> bytes:
        if sequence != self.expected_sequence:
            return response_frame(command, sequence, status=Status.SEQUENCE, response=Command.NACK)

        if command in self.drop_once:
            self.drop_once.remove(command)
            self.expected_sequence = (self.expected_sequence + 1) & 0xFFFF
            return b""

        if command in self.nack_once:
            status = self.nack_once.pop(command)
            self.expected_sequence = (self.expected_sequence + 1) & 0xFFFF
            return response_frame(command, sequence, status=status, response=Command.NACK)

        if command == Command.HELLO or command == Command.GET_INFO:
            extra = info_extra()
        elif command == Command.GET_STATUS:
            extra = status_extra(expected_sequence=self.expected_sequence)
        elif command == Command.BEGIN_UPDATE:
            assert len(payload) == 512
            extra = b""
        elif command == Command.WRITE_BLOCK:
            offset = struct.unpack_from("<I", payload, 0)[0]
            if offset != self.update_offset:
                self.expected_sequence = (self.expected_sequence + 1) & 0xFFFF
                return response_frame(
                    command,
                    sequence,
                    status=Status.SEQUENCE,
                    response=Command.NACK,
                )
            self.update_offset += len(payload) - 4
            self.blocks += 1
            extra = b""
        elif command == Command.FINISH_UPDATE:
            extra = status_extra(
                session_state=2,
                expected_sequence=self.expected_sequence,
                installer_state=4,
                accepted_payload=self.update_offset,
                image_version=3,
                blocks=self.blocks,
            )
        elif command == Command.ABORT_UPDATE:
            extra = b""
        elif command == Command.RESET:
            extra = b""
        else:
            self.expected_sequence = (self.expected_sequence + 1) & 0xFFFF
            return response_frame(
                command,
                sequence,
                status=Status.UNKNOWN_COMMAND,
                response=Command.NACK,
            )

        self.expected_sequence = (self.expected_sequence + 1) & 0xFFFF
        return response_frame(command, sequence, extra=extra)


def test_fake_serial_valid_info_response() -> None:
    serial = ScriptedSerial([response_frame(Command.HELLO, 0, extra=info_extra())])
    info = Stm32Client(serial, timeout=0.01).hello()

    assert info.max_write_data_size == 1020
    assert decode_frame(serial.writes[0]).command == Command.HELLO


def test_timeout() -> None:
    client = Stm32Client(ScriptedSerial(), timeout=0.01, retries=0)

    with pytest.raises(ProtocolTimeout):
        client.hello()


def test_cli_and_client_default_timeout_match_hardware_budget() -> None:
    assert DEFAULT_TIMEOUT == DEFAULT_RESPONSE_TIMEOUT_SECONDS
    assert DEFAULT_TIMEOUT >= 15.0


def test_bad_crc_response() -> None:
    response = bytearray(response_frame(Command.HELLO, 0, extra=info_extra()))
    response[-1] ^= 0x01
    client = Stm32Client(ScriptedSerial([bytes(response)]), timeout=0.01)

    with pytest.raises(CrcError):
        client.hello()


def test_nack_response() -> None:
    response = response_frame(
        Command.GET_STATUS,
        1,
        status=Status.STATE,
        response=Command.NACK,
    )
    serial = ScriptedSerial([response_frame(Command.HELLO, 0, extra=info_extra()), response])
    client = Stm32Client(serial, timeout=0.01)
    client.hello()

    with pytest.raises(NackError) as excinfo:
        client.get_status()

    assert excinfo.value.status == Status.STATE


def test_lost_write_ack_does_not_duplicate_block(tmp_path: Path) -> None:
    package = make_package(tmp_path, size=256)
    serial = FakeTargetSerial(drop_once={Command.WRITE_BLOCK})
    client = Stm32Client(serial, timeout=0.01, retries=2)

    with pytest.raises(ProtocolTimeout):
        client.update(package, block_size=128)

    write_blocks = [
        decode_frame(write).payload
        for write in serial.writes
        if decode_frame(write).command == Command.WRITE_BLOCK
    ]
    assert len(write_blocks) == 1
    assert struct.unpack_from("<I", write_blocks[0], 0)[0] == 0


def test_duplicate_block_nack() -> None:
    serial = FakeTargetSerial()
    client = Stm32Client(serial, timeout=0.01)
    client.hello()
    client.request(Command.BEGIN_UPDATE, b"\xFF" * 512)
    client.request(Command.WRITE_BLOCK, struct.pack("<I", 0) + b"abc")

    with pytest.raises(NackError) as excinfo:
        client.request(Command.WRITE_BLOCK, struct.pack("<I", 0) + b"abc")

    assert excinfo.value.status == Status.SEQUENCE


def test_abort_after_update_error(tmp_path: Path) -> None:
    package = make_package(tmp_path, size=256)
    serial = FakeTargetSerial(nack_once={Command.WRITE_BLOCK: Status.FLASH})
    client = Stm32Client(serial, timeout=0.01)

    with pytest.raises(NackError):
        client.update(package, block_size=128)

    assert Command.ABORT_UPDATE in serial.commands


def test_complete_simulated_update(tmp_path: Path) -> None:
    package = make_package(tmp_path, size=1536)
    serial = FakeTargetSerial()
    progress: list[tuple[int, int]] = []
    client = Stm32Client(serial, timeout=0.01)

    result = client.update(package, block_size=512, progress=lambda done, total: progress.append((done, total)))

    assert result.image_version == 3
    assert result.payload_size == 1536
    assert result.programmed_block_count == 3
    assert progress[-1] == (1536, 1536)
    assert serial.commands == [
        Command.HELLO,
        Command.BEGIN_UPDATE,
        Command.WRITE_BLOCK,
        Command.WRITE_BLOCK,
        Command.WRITE_BLOCK,
        Command.FINISH_UPDATE,
    ]


def test_reset_command() -> None:
    serial = FakeTargetSerial()
    client = Stm32Client(serial, timeout=0.01)
    client.hello()
    client.reset()

    assert serial.commands[-1] == Command.RESET


def test_reset_command_does_not_require_ack() -> None:
    serial = ScriptedSerial([response_frame(Command.HELLO, 0, extra=info_extra())])
    client = Stm32Client(serial, timeout=0.01)
    client.hello()
    client.reset()

    assert decode_frame(serial.writes[-1]).command == Command.RESET
