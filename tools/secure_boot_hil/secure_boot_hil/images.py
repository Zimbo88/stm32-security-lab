"""Signed-image parsing and deterministic fault-image generation."""

from __future__ import annotations

import hashlib
import json
import random
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .build import sha256_bytes, sha256_file
from .errors import ConfigError
from .logging import utc_now
from .model import Artifact, MutationRecord, MutationType

MANIFEST_STRUCT = struct.Struct("<8I64s")
SIGNED_IMAGE_MAGIC = 0x31474953
UPDATE_PACKAGE_FORMAT_VERSION = 2
UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1 = 0xF429AB01
UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION = 1


@dataclass(frozen=True)
class SignedManifest:
    magic: int
    header_version: int
    image_version: int
    vector_address: int
    image_size: int
    flags: int
    reserved0: int
    reserved1: int
    payload_sha512: bytes

    def to_json(self) -> dict[str, Any]:
        return {
            "magic": self.magic,
            "magic_hex": f"0x{self.magic:08X}",
            "header_version": self.header_version,
            "image_version": self.image_version,
            "vector_address": self.vector_address,
            "vector_address_hex": f"0x{self.vector_address:08X}",
            "image_size": self.image_size,
            "flags": self.flags,
            "flags_hex": f"0x{self.flags:08X}",
            "reserved0": self.reserved0,
            "reserved0_hex": f"0x{self.reserved0:08X}",
            "reserved1": self.reserved1,
            "reserved1_hex": f"0x{self.reserved1:08X}",
            "payload_sha512": self.payload_sha512.hex(),
        }


def parse_manifest(data: bytes, *, manifest_size: int) -> SignedManifest:
    if manifest_size != MANIFEST_STRUCT.size:
        raise ConfigError("HIL manifest parser expects the repository v2 96-byte manifest")
    if len(data) < manifest_size:
        raise ValueError("signed image is too short to contain a manifest")
    fields = MANIFEST_STRUCT.unpack_from(data, 0)
    return SignedManifest(*fields)


def inspect_signed_artifact(path: Path, layout: dict[str, Any]) -> dict[str, Any]:
    image = path.read_bytes()
    manifest = parse_manifest(image, manifest_size=int(layout["signed_manifest_size"]))
    header_size = int(layout["signed_image_header_size"])
    payload = image[header_size:]
    return {
        "path": path.as_posix(),
        "size": len(image),
        "sha256": sha256_bytes(image),
        "manifest": manifest.to_json(),
        "payload_sha512_observed": hashlib.sha512(payload).hexdigest(),
        "payload_size_observed": len(payload),
    }


def _write_mutation(
    source: Path,
    output: Path,
    data: bytearray,
    *,
    mutation_type: MutationType,
    byte_offset: int,
    old_value: int,
    new_value: int,
    rationale: str,
) -> MutationRecord:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bytes(data))
    return MutationRecord(
        source_path=source,
        output_path=output,
        mutation_type=mutation_type,
        source_sha256=sha256_file(source),
        output_sha256=sha256_file(output),
        byte_offset=byte_offset,
        old_value=old_value,
        new_value=new_value,
        rationale=rationale,
    )


def _flip_byte(data: bytearray, offset: int) -> tuple[int, int]:
    if not (0 <= offset < len(data)):
        raise ValueError(f"mutation offset outside image: {offset}")
    old = data[offset]
    data[offset] ^= 0x01
    return old, data[offset]


def mutate_signed_image(
    source: Path,
    output: Path,
    mutation_type: MutationType,
    layout: dict[str, Any],
    *,
    seed: str | None = None,
    campaign_index: int = 0,
) -> MutationRecord:
    data = bytearray(source.read_bytes())
    manifest_size = int(layout["signed_manifest_size"])
    signature_size = int(layout["signed_signature_size"])
    header_size = int(layout["signed_image_header_size"])

    if len(data) < header_size:
        raise ValueError("source signed image is shorter than the signed-image header")

    if mutation_type == MutationType.BAD_SIGNATURE:
        offset = manifest_size
        old, new = _flip_byte(data, offset)
        rationale = "Flip one authenticated signature byte to force Ed25519 rejection."
    elif mutation_type == MutationType.MODIFIED_PAYLOAD:
        safe_payload_prefix = 0x100
        offset = header_size + safe_payload_prefix

        if offset >= len(data):
            raise ValueError(
                "source signed image is too short for a safe payload mutation"
            )

        old, new = _flip_byte(data, offset)

        rationale = (
            "Flip one payload byte after the vector table "
            "to force SHA-512 rejection."
        )
    elif mutation_type == MutationType.ERASED_HEADER:
        offset = 0
        old = data[offset]
        data[:header_size] = b"\xff" * header_size
        new = data[offset]
        rationale = "Erase the full signed-image header to model erased flash."
    elif mutation_type == MutationType.ZERO_HEADER:
        offset = 0
        old = data[offset]
        data[:header_size] = b"\x00" * header_size
        new = data[offset]
        rationale = "Zero the full signed-image header to model malformed flash."
    elif mutation_type == MutationType.SEEDED_BIT_FLIP:
        if seed is None:
            raise ValueError("seeded mutation requires a campaign seed")
        mutable_start = manifest_size + signature_size
        rng = random.Random(f"{seed}:{campaign_index}:{sha256_file(source)}")
        offset = rng.randrange(mutable_start, len(data))
        bit = 1 << rng.randrange(0, 8)
        old = data[offset]
        data[offset] ^= bit
        new = data[offset]
        rationale = "Deterministic seeded bit flip for reproducible campaigns."
    else:
        raise ValueError(f"unsupported signed-image mutation: {mutation_type.value}")

    return _write_mutation(
        source,
        output,
        data,
        mutation_type=mutation_type,
        byte_offset=offset,
        old_value=old,
        new_value=new,
        rationale=rationale,
    )


def mutate_region_fill(
    source: Path,
    output: Path,
    mutation_type: MutationType,
    *,
    fill: int,
    rationale: str,
) -> MutationRecord:
    if not (0 <= fill <= 0xFF):
        raise ValueError("fill byte must fit in uint8")
    original = source.read_bytes()
    if not original:
        raise ValueError("source region image is empty")
    data = bytearray(bytes([fill]) * len(original))
    return _write_mutation(
        source,
        output,
        data,
        mutation_type=mutation_type,
        byte_offset=0,
        old_value=original[0],
        new_value=fill,
        rationale=rationale,
    )


def artifact_manifest(path: Path, artifacts: list[Artifact]) -> None:
    payload = {
        "created_utc": utc_now(),
        "artifacts": [artifact.to_json() for artifact in artifacts],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def mutation_manifest(path: Path, records: list[MutationRecord]) -> None:
    payload = {
        "created_utc": utc_now(),
        "mutations": [record.to_json() for record in records],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
