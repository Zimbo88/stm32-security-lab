#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
import tempfile
from pathlib import Path

from nacl.signing import SigningKey

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from stm32f429_layout import LAYOUT  # noqa: E402

SIGNED_IMAGE_MAGIC = 0x31474953
SIGNED_HEADER_VERSION = 1
UPDATE_PACKAGE_FORMAT_VERSION = 2
UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1 = 0xF429AB01
UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION = 1
IMAGE_VERSION = 2

SIGNED_IMAGE_BASE = LAYOUT["signed_image_base"]
APPLICATION_BASE = LAYOUT["application_base"]
APPLICATION_MSP_BASE = LAYOUT["application_msp_base"]
APPLICATION_MSP_END = LAYOUT["application_msp_end"]
APPLICATION_MSP_ALIGNMENT = LAYOUT["application_msp_alignment"]
APPLICATION_FLASH_END = LAYOUT["application_flash_end"]
APPLICATION_MIN_SIZE = LAYOUT["application_min_payload_size"]
MAX_PAYLOAD_SIZE = LAYOUT["application_payload_max_size"]
SIGNED_IMAGE_FLAGS_ALLOWED_MASK = LAYOUT["signed_image_flags_allowed_mask"]

MANIFEST_SIZE = LAYOUT["signed_manifest_size"]
SIGNATURE_SIZE = LAYOUT["signed_signature_size"]
APPLICATION_OFFSET = LAYOUT["signed_image_header_size"]
UINT32_MAX = 0xFFFFFFFF
MANIFEST_STRUCT = struct.Struct("<8I64s")


class SigningError(ValueError):
    pass


def require_u32(value: int, name: str) -> int:
    if not isinstance(value, int):
        raise SigningError(f"{name} must be an integer")
    if not (0 <= value <= UINT32_MAX):
        raise SigningError(f"{name} must be between 0 and {UINT32_MAX}")
    return value


def checked_u32_add(left: int, right: int, name: str) -> int:
    require_u32(left, f"{name} base")
    require_u32(right, f"{name} size")

    if right > UINT32_MAX - left:
        raise SigningError(f"{name} overflows the 32-bit address space")

    return left + right


def read_file(path: Path, label: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise SigningError(f"Failed to read {label} '{path}': {exc}") from exc


def validate_seed(seed: bytes) -> None:
    if len(seed) != 32:
        raise SigningError(f"Ed25519 seed must contain 32 bytes, got {len(seed)}")


def slot_layout(slot: str) -> dict[str, int]:
    normalized = slot.lower()
    if normalized not in ("a", "b"):
        raise SigningError(f"Unknown slot '{slot}'")
    return LAYOUT[f"slot_{normalized}"]


def validate_payload_size(
    application_size: int,
    *,
    vector_address: int = APPLICATION_BASE,
    flash_end: int = APPLICATION_FLASH_END,
    max_payload_size: int = MAX_PAYLOAD_SIZE,
) -> int:
    require_u32(application_size, "Application size")
    require_u32(vector_address, "Application vector address")
    require_u32(flash_end, "Application flash end")
    require_u32(max_payload_size, "Application maximum payload size")

    if vector_address >= flash_end:
        raise SigningError("Application vector address is outside the slot")

    if application_size < APPLICATION_MIN_SIZE:
        raise SigningError("Application is too small to contain a vector table")

    if application_size > max_payload_size:
        raise SigningError(
            "Application exceeds bootloader-supported application region: "
            f"{application_size} bytes > {max_payload_size} bytes"
        )

    payload_end = checked_u32_add(
        vector_address,
        application_size,
        "Application address range",
    )

    if payload_end > flash_end:
        raise SigningError(
            "Application extends beyond supported flash region: "
            f"0x{payload_end:08X} > 0x{flash_end:08X}"
        )

    return payload_end


def validate_vector_table(
    application: bytes,
    *,
    vector_address: int = APPLICATION_BASE,
    flash_end: int = APPLICATION_FLASH_END,
    max_payload_size: int = MAX_PAYLOAD_SIZE,
) -> tuple[int, int]:
    payload_end = validate_payload_size(
        len(application),
        vector_address=vector_address,
        flash_end=flash_end,
        max_payload_size=max_payload_size,
    )
    initial_msp, reset_vector = struct.unpack_from("<II", application, 0)

    if not (APPLICATION_MSP_BASE < initial_msp <= APPLICATION_MSP_END):
        raise SigningError(
            "Initial MSP is outside supported SRAM range: "
            f"0x{initial_msp:08X}"
        )

    if (initial_msp & (APPLICATION_MSP_ALIGNMENT - 1)) != 0:
        raise SigningError(
            "Initial MSP is not aligned to "
            f"{APPLICATION_MSP_ALIGNMENT} bytes: 0x{initial_msp:08X}"
        )

    if (reset_vector & 1) == 0:
        raise SigningError(
            f"Reset vector does not have the Thumb bit: 0x{reset_vector:08X}"
        )

    reset_address = reset_vector & ~1

    if not (vector_address <= reset_address < payload_end):
        raise SigningError(
            "Reset vector is outside application payload: "
            f"0x{reset_vector:08X}"
        )

    return initial_msp, reset_vector


def build_manifest(
    *,
    image_version: int,
    header_version: int,
    image_size: int,
    payload_hash: bytes,
    flags: int = 0,
    reserved0: int = 0,
    reserved1: int = 0,
    vector_address: int = APPLICATION_BASE,
    update_package: bool = False,
) -> bytes:
    require_u32(header_version, "Header version")
    require_u32(image_version, "Image version")
    require_u32(image_size, "Application size")
    require_u32(flags, "Manifest flags")
    require_u32(reserved0, "Reserved field 0")
    require_u32(reserved1, "Reserved field 1")
    require_u32(vector_address, "Application vector address")

    expected_header_version = (
        UPDATE_PACKAGE_FORMAT_VERSION
        if update_package
        else SIGNED_HEADER_VERSION
    )
    if header_version != expected_header_version:
        raise SigningError(
            "Unsupported signed-image header version: "
            f"{header_version} (expected {expected_header_version})"
        )

    unsupported_flags = flags & ~SIGNED_IMAGE_FLAGS_ALLOWED_MASK
    if unsupported_flags != 0:
        raise SigningError(
            "Unsupported signed-image flags: "
            f"0x{unsupported_flags:08X}"
        )

    if update_package:
        if reserved0 != UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1:
            raise SigningError("Update package target compatibility is invalid")
        if reserved1 != UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION:
            raise SigningError("Update package image type is invalid")
    elif reserved0 != 0 or reserved1 != 0:
        raise SigningError("Reserved manifest fields must be zero")

    if len(payload_hash) != 64:
        raise SigningError(
            f"Payload SHA-512 must contain 64 bytes, got {len(payload_hash)}"
        )

    manifest = MANIFEST_STRUCT.pack(
        SIGNED_IMAGE_MAGIC,
        header_version,
        image_version,
        vector_address,
        image_size,
        flags,
        reserved0,
        reserved1,
        payload_hash,
    )

    if len(manifest) != MANIFEST_SIZE:
        raise SigningError(f"Internal manifest-size error: {len(manifest)}")

    return manifest


def build_signed_image(
    application: bytes,
    seed: bytes,
    *,
    image_version: int = IMAGE_VERSION,
    header_version: int = SIGNED_HEADER_VERSION,
    flags: int = 0,
    reserved0: int = 0,
    reserved1: int = 0,
    vector_address: int = APPLICATION_BASE,
    flash_end: int = APPLICATION_FLASH_END,
    max_payload_size: int = MAX_PAYLOAD_SIZE,
    update_package: bool = False,
) -> tuple[bytes, bytes, int, int]:
    validate_seed(seed)
    initial_msp, reset_vector = validate_vector_table(
        application,
        vector_address=vector_address,
        flash_end=flash_end,
        max_payload_size=max_payload_size,
    )
    payload_hash = hashlib.sha512(application).digest()
    manifest = build_manifest(
        image_version=image_version,
        header_version=header_version,
        image_size=len(application),
        payload_hash=payload_hash,
        flags=flags,
        reserved0=reserved0,
        reserved1=reserved1,
        vector_address=vector_address,
        update_package=update_package,
    )

    signing_key = SigningKey(seed)
    signature = signing_key.sign(manifest).signature

    if len(signature) != SIGNATURE_SIZE:
        raise SigningError(f"Internal signature-size error: {len(signature)}")

    padding_size = APPLICATION_OFFSET - MANIFEST_SIZE - SIGNATURE_SIZE

    if padding_size < 0:
        raise SigningError("Application overlaps manifest or signature")

    combined_image = (
        manifest
        + signature
        + (b"\xFF" * padding_size)
        + application
    )

    return combined_image, payload_hash, initial_msp, reset_vector


def build_update_package(
    application: bytes,
    seed: bytes,
    *,
    slot: str,
    image_version: int = IMAGE_VERSION,
) -> tuple[bytes, bytes, int, int]:
    target_slot = slot_layout(slot)
    return build_signed_image(
        application,
        seed,
        image_version=image_version,
        header_version=UPDATE_PACKAGE_FORMAT_VERSION,
        reserved0=UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1,
        reserved1=UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION,
        vector_address=target_slot["payload_base"],
        flash_end=target_slot["end"],
        max_payload_size=target_slot["payload_max_size"],
        update_package=True,
    )


def write_output_atomically(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    temp_name = ""
    try:
        fd, temp_name = tempfile.mkstemp(
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
        )
        temp_path = Path(temp_name)

        with os.fdopen(fd, "wb") as temp_file:
            temp_file.write(data)
            temp_file.flush()
            os.fsync(temp_file.fileno())

        os.replace(temp_path, path)
    except OSError as exc:
        if temp_name:
            try:
                Path(temp_name).unlink(missing_ok=True)
            except OSError:
                pass
        raise SigningError(f"Failed to write output atomically: {exc}") from exc


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--application", required=True, type=Path)
    parser.add_argument("--seed", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--image-version",
        type=int,
        default=IMAGE_VERSION,
        help=f"Image version stored in the signed manifest (default: {IMAGE_VERSION})",
    )
    parser.add_argument(
        "--header-version",
        type=int,
        default=SIGNED_HEADER_VERSION,
        help=f"Header version stored in the signed manifest (default: {SIGNED_HEADER_VERSION})",
    )
    args = parser.parse_args()

    if not (0 <= args.image_version <= 0xFFFFFFFF):
        raise SystemExit("--image-version must be between 0 and 4294967295")

    if not (0 <= args.header_version <= 0xFFFFFFFF):
        raise SystemExit("--header-version must be between 0 and 4294967295")

    try:
        application = read_file(args.application, "application")
        seed = read_file(args.seed, "Ed25519 seed")
        combined_image, payload_hash, initial_msp, reset_vector = (
            build_signed_image(
                application,
                seed,
                image_version=args.image_version,
                header_version=args.header_version,
            )
        )
        write_output_atomically(args.output, combined_image)
        public_key = bytes(SigningKey(seed).verify_key)
    except SigningError as exc:
        raise SystemExit(str(exc)) from exc

    print(f"Manifest address : 0x{SIGNED_IMAGE_BASE:08X}")
    print(f"Signature address: 0x{SIGNED_IMAGE_BASE + MANIFEST_SIZE:08X}")
    print(f"Application addr : 0x{APPLICATION_BASE:08X}")
    print(f"MCU              : {LAYOUT['mcu']}")
    print(f"Layout profile   : {LAYOUT['profile']}")
    print(f"Flash range      : 0x{LAYOUT['flash_base']:08X}-0x{LAYOUT['flash_end']:08X}")
    print(f"Header version   : {args.header_version}")
    print(f"Image version    : {args.image_version}")
    print(f"Application size : {len(application)} bytes")
    print(f"Combined size    : {len(combined_image)} bytes")
    print(f"Initial MSP      : 0x{initial_msp:08X}")
    print(f"Reset vector     : 0x{reset_vector:08X}")
    print(f"Payload SHA-512  : {payload_hash.hex()}")
    print(f"Public key       : {public_key.hex()}")
    print(f"Output           : {args.output}")


if __name__ == "__main__":
    main()
