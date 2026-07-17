#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

from nacl.signing import SigningKey


REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from stm32f429_layout import LAYOUT  # noqa: E402


SIGNED_IMAGE_MAGIC = 0x31474953
SIGNED_HEADER_VERSION = 1
IMAGE_VERSION = 2

SIGNED_IMAGE_BASE = LAYOUT["signed_image_base"]
APPLICATION_BASE = LAYOUT["application_base"]
APPLICATION_MSP_BASE = LAYOUT["application_msp_base"]
APPLICATION_MSP_END = LAYOUT["application_msp_end"]

MANIFEST_SIZE = LAYOUT["signed_manifest_size"]
SIGNATURE_SIZE = LAYOUT["signed_signature_size"]
APPLICATION_OFFSET = LAYOUT["signed_image_header_size"]


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

    application = args.application.read_bytes()
    seed = args.seed.read_bytes()

    if len(seed) != 32:
        raise SystemExit(
            f"Ed25519 seed must contain 32 bytes, got {len(seed)}"
        )

    if len(application) < 8:
        raise SystemExit("Application is too small to contain a vector table")

    initial_msp, reset_vector = struct.unpack_from("<II", application, 0)

    if not (APPLICATION_MSP_BASE <= initial_msp <= APPLICATION_MSP_END):
        raise SystemExit(
            f"Initial MSP is outside SRAM: 0x{initial_msp:08X}"
        )

    if (reset_vector & 1) == 0:
        raise SystemExit(
            f"Reset vector does not have the Thumb bit: "
            f"0x{reset_vector:08X}"
        )

    reset_address = reset_vector & ~1

    if not (
        APPLICATION_BASE
        <= reset_address
        < APPLICATION_BASE + len(application)
    ):
        raise SystemExit(
            f"Reset vector is outside application: "
            f"0x{reset_vector:08X}"
        )

    payload_hash = hashlib.sha512(application).digest()

    manifest = struct.pack(
        "<8I64s",
        SIGNED_IMAGE_MAGIC,
        args.header_version,
        args.image_version,
        APPLICATION_BASE,
        len(application),
        0,
        0,
        0,
        payload_hash,
    )

    if len(manifest) != MANIFEST_SIZE:
        raise SystemExit(
            f"Internal manifest-size error: {len(manifest)}"
        )

    signing_key = SigningKey(seed)
    signature = signing_key.sign(manifest).signature

    if len(signature) != SIGNATURE_SIZE:
        raise SystemExit(
            f"Internal signature-size error: {len(signature)}"
        )

    padding_size = APPLICATION_OFFSET - MANIFEST_SIZE - SIGNATURE_SIZE

    if padding_size < 0:
        raise SystemExit("Application overlaps manifest or signature")

    combined_image = (
        manifest
        + signature
        + (b"\xFF" * padding_size)
        + application
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(combined_image)

    print(f"Manifest address : 0x{SIGNED_IMAGE_BASE:08X}")
    print(f"Signature address: 0x{SIGNED_IMAGE_BASE + MANIFEST_SIZE:08X}")
    print(f"Application addr : 0x{APPLICATION_BASE:08X}")
    print(f"Header version   : {args.header_version}")
    print(f"Image version    : {args.image_version}")
    print(f"Application size : {len(application)} bytes")
    print(f"Combined size    : {len(combined_image)} bytes")
    print(f"Initial MSP      : 0x{initial_msp:08X}")
    print(f"Reset vector     : 0x{reset_vector:08X}")
    print(f"Payload SHA-512  : {payload_hash.hex()}")
    print(f"Public key       : {bytes(signing_key.verify_key).hex()}")
    print(f"Output           : {args.output}")


if __name__ == "__main__":
    main()
