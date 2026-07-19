from __future__ import annotations

import struct
from pathlib import Path

from secure_boot_hil.images import (
    SIGNED_IMAGE_MAGIC,
    UPDATE_PACKAGE_FORMAT_VERSION,
    inspect_signed_artifact,
    mutate_signed_image,
    parse_manifest,
)
from secure_boot_hil.model import MutationType


def layout() -> dict[str, int]:
    return {
        "signed_manifest_size": 96,
        "signed_signature_size": 64,
        "signed_image_header_size": 512,
    }


def signed_image_bytes() -> bytes:
    manifest = struct.pack(
        "<8I64s",
        SIGNED_IMAGE_MAGIC,
        UPDATE_PACKAGE_FORMAT_VERSION,
        2,
        0x08020200,
        16,
        0,
        0xF429AB01,
        1,
        bytes(64),
    )
    return manifest + bytes([0xA5]) * 64 + bytes([0xFF]) * (512 - 160) + bytes(range(16))


def test_manifest_parser_uses_version2_layout() -> None:
    manifest = parse_manifest(signed_image_bytes(), manifest_size=96)
    assert manifest.magic == SIGNED_IMAGE_MAGIC
    assert manifest.header_version == UPDATE_PACKAGE_FORMAT_VERSION
    assert manifest.vector_address == 0x08020200


def test_mutation_generation_is_immutable(tmp_path: Path) -> None:
    source = tmp_path / "slot_a.bin"
    output = tmp_path / "slot_a_bad_signature.bin"
    original = signed_image_bytes()
    source.write_bytes(original)
    record = mutate_signed_image(source, output, MutationType.BAD_SIGNATURE, layout())
    assert source.read_bytes() == original
    assert output.read_bytes() != original
    assert record.byte_offset == 96
    assert record.source_sha256 != record.output_sha256


def test_seeded_mutation_is_deterministic(tmp_path: Path) -> None:
    source = tmp_path / "slot_a.bin"
    source.write_bytes(signed_image_bytes())
    first = mutate_signed_image(
        source,
        tmp_path / "first.bin",
        MutationType.SEEDED_BIT_FLIP,
        layout(),
        seed="campaign",
        campaign_index=7,
    )
    second = mutate_signed_image(
        source,
        tmp_path / "second.bin",
        MutationType.SEEDED_BIT_FLIP,
        layout(),
        seed="campaign",
        campaign_index=7,
    )
    assert first.byte_offset == second.byte_offset
    assert first.output_sha256 == second.output_sha256


def test_inspect_signed_artifact_reports_hashes(tmp_path: Path) -> None:
    source = tmp_path / "slot_a.bin"
    source.write_bytes(signed_image_bytes())
    report = inspect_signed_artifact(source, layout())
    assert report["size"] == len(signed_image_bytes())
    assert report["manifest"]["header_version"] == UPDATE_PACKAGE_FORMAT_VERSION
