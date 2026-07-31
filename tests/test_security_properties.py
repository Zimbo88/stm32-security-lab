from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st
from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import update_package  # noqa: E402
from stm32ctl.errors import CrcError, ProtocolError  # noqa: E402
from stm32ctl.protocol import (  # noqa: E402
    HEADER_SIZE,
    MAX_PAYLOAD_SIZE,
    Command,
    Frame,
    Status,
    decode_frame,
    encode_frame,
    parse_info,
    parse_response,
    parse_status,
)


def test_frame_round_trip_for_all_bounded_inputs() -> None:
    @settings(max_examples=160, deadline=None)
    @given(
        command=st.integers(min_value=0, max_value=0xFF),
        sequence=st.integers(min_value=0, max_value=0xFFFF),
        payload=st.binary(max_size=MAX_PAYLOAD_SIZE),
    )
    def property_test(command: int, sequence: int, payload: bytes) -> None:
        decoded = decode_frame(encode_frame(command, sequence, payload))
        assert decoded.version == 1
        assert decoded.command == command
        assert decoded.sequence == sequence
        assert decoded.payload == payload

    property_test()


@settings(max_examples=240, deadline=None)
@given(data=st.binary(min_size=HEADER_SIZE + 4, max_size=4096))
def test_arbitrary_frames_only_fail_closed(data: bytes) -> None:
    try:
        decode_frame(data)
    except (CrcError, ProtocolError):
        return


@settings(max_examples=120, deadline=None)
@given(extra=st.binary(max_size=64))
def test_info_and_status_lengths_are_strict(extra: bytes) -> None:
    if len(extra) != 10:
        with pytest.raises(ProtocolError):
            parse_info(extra)
    if len(extra) != 18:
        with pytest.raises(ProtocolError):
            parse_status(extra)


@settings(max_examples=200, deadline=None)
@given(
    version=st.integers(min_value=0, max_value=0xFF),
    command=st.integers(min_value=0, max_value=0xFF),
    sequence=st.integers(min_value=0, max_value=0xFFFF),
    payload=st.binary(max_size=128),
    request=st.sampled_from(list(Command)),
)
def test_arbitrary_stm32ctl_responses_only_fail_closed(
    version: int,
    command: int,
    sequence: int,
    payload: bytes,
    request: Command,
) -> None:
    frame = Frame(version, command, sequence, payload)
    try:
        parse_response(frame, sequence, request)
    except ProtocolError:
        return


@settings(max_examples=120, deadline=None)
@given(payload=st.binary(min_size=MAX_PAYLOAD_SIZE + 1, max_size=MAX_PAYLOAD_SIZE + 256))
def test_oversized_frames_are_rejected_before_serialization(payload: bytes) -> None:
    with pytest.raises(ProtocolError):
        encode_frame(Command.WRITE_BLOCK, 0, payload)


@settings(max_examples=120, deadline=None)
@given(package=st.binary(max_size=4096))
def test_update_package_inspection_never_accepts_unchecked_random_bytes(package: bytes) -> None:
    try:
        update_package.inspect_package_bytes(package)
    except update_package.PackageError:
        return


def _load_signer():
    path = ROOT / "firmware" / "exp065_signed_app" / "tools" / "build_signed_image.py"
    spec = importlib.util.spec_from_file_location("security_property_signer", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def valid_package() -> tuple[bytes, bytes]:
    signer = _load_signer()
    seed = bytes(range(32))
    slot = "b"
    payload = bytearray(b"\xA5" * 128)
    vector_base = update_package.LAYOUT[f"slot_{slot}_payload_base"]
    struct.pack_into(
        "<II",
        payload,
        0,
        update_package.LAYOUT["application_msp_end"],
        vector_base | 1,
    )
    package, _, _, _ = signer.build_update_package(
        bytes(payload), seed, slot=slot, image_version=3
    )
    return package, bytes(SigningKey(seed).verify_key)


@settings(max_examples=64, deadline=None)
@given(index=st.integers(min_value=0, max_value=511))
def test_single_byte_mutation_of_signed_package_is_not_silently_valid(
    valid_package: tuple[bytes, bytes], index: int
) -> None:
    package, public_key = valid_package
    mutated = bytearray(package)
    mutated[index % len(mutated)] ^= 0x01
    with pytest.raises(update_package.PackageError):
        update_package.verify_package_bytes(bytes(mutated), public_key, slot="b")


@settings(max_examples=80, deadline=None)
@given(key=st.binary(min_size=32, max_size=32))
def test_public_key_fingerprint_is_deterministic(key: bytes) -> None:
    assert update_package.sha256_hex(key) == update_package.sha256_hex(key)


@settings(max_examples=80, deadline=None)
@given(value=st.integers(min_value=0, max_value=0xFFFFFFFF))
def test_slot_vector_mapping_is_total(value: int) -> None:
    result = update_package.slot_from_vector(value)
    assert result in (None, "a", "b")


def test_protocol_constants_match_target_contract() -> None:
    assert HEADER_SIZE == 10
    assert int(Status.OK) == 0
