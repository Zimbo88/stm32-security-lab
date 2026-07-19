#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
import struct
import sys
import zlib
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
SIGNER_PATH = (
    ROOT
    / "firmware"
    / "exp065_signed_app"
    / "tools"
    / "build_signed_image.py"
)

sys.path.insert(0, str(ROOT / "tools"))

import release_artifacts  # noqa: E402
from stm32f429_layout import LAYOUT  # noqa: E402


spec = importlib.util.spec_from_file_location("build_signed_image", SIGNER_PATH)
if spec is None or spec.loader is None:
    raise RuntimeError("failed to load signed-image builder")
signer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(signer)


METADATA_MAGIC = 0x314D5442
METADATA_COMMIT0 = 0xC0DEF00D
METADATA_COMMIT1 = 0x3F210FF2
BOOT_SLOT_NONE = 0xFFFFFFFF
STATE_WRITING = 1
STATE_CANDIDATE_READY = 2
STATE_CONFIRMED = 4

EXIT_OK = 0
EXIT_FAILED = 1


class PackageError(ValueError):
    pass


def read_file(path: Path, label: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise PackageError(f"failed to read {label} '{path}': {exc}") from exc


def write_json(path: Path | None, data: dict[str, Any]) -> None:
    text = json.dumps(data, indent=2, sort_keys=True) + "\n"
    if path is None:
        sys.stdout.write(text)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="ascii")


def public_key_from_args(args: argparse.Namespace) -> bytes:
    try:
        return release_artifacts.public_key_from_args(args)
    except release_artifacts.VerificationError as exc:
        raise PackageError(str(exc)) from exc


def slot_from_vector(vector_address: int) -> str | None:
    for slot in ("a", "b"):
        if LAYOUT[f"slot_{slot}_payload_base"] == vector_address:
            return slot
    return None


def inspect_package_bytes(package: bytes) -> dict[str, Any]:
    manifest_size = LAYOUT["signed_manifest_size"]
    signature_size = LAYOUT["signed_signature_size"]
    header_size = LAYOUT["signed_image_header_size"]

    if len(package) < manifest_size + signature_size:
        raise PackageError("truncated package header")

    manifest_bytes = package[:manifest_size]
    manifest = release_artifacts.decode_manifest(manifest_bytes)
    slot = slot_from_vector(manifest["vector_address"])

    if manifest["header_version"] != signer.UPDATE_PACKAGE_FORMAT_VERSION:
        raise PackageError("unsupported update package version")
    if manifest["reserved0"] != signer.UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1:
        raise PackageError("update package target compatibility is invalid")
    if manifest["reserved1"] != signer.UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION:
        raise PackageError("update package image type is invalid")
    if manifest["flags"] & ~LAYOUT["signed_image_flags_allowed_mask"]:
        raise PackageError("unsupported update package flags")
    if slot is None:
        raise PackageError("manifest vector address is not a known slot payload base")

    expected_size = header_size + manifest["image_size"]
    if len(package) < expected_size:
        raise PackageError("truncated package")
    if len(package) > expected_size:
        raise PackageError("package contains trailing data")

    padding = package[manifest_size + signature_size:header_size]
    if any(byte != 0xFF for byte in padding):
        raise PackageError("signed-image padding is not canonical 0xff")

    return {
        "format_version": manifest["header_version"],
        "target_compatibility": manifest["reserved0"],
        "image_type": manifest["reserved1"],
        "slot": slot,
        "image_version": manifest["image_version"],
        "payload_size": manifest["image_size"],
        "payload_sha512": manifest["payload_sha512"],
        "vector_address": manifest["vector_address"],
        "package_size": len(package),
        "signed_header_size": header_size,
    }


def verify_package_bytes(
    package: bytes,
    public_key: bytes,
    *,
    slot: str,
    application: bytes | None = None,
) -> dict[str, Any]:
    try:
        inspected = inspect_package_bytes(package)
        if inspected["slot"] != slot:
            raise PackageError(
                f"package is for slot {inspected['slot']}, not requested slot {slot}"
            )
        verified = release_artifacts.verify_signed_image(
            package,
            public_key,
            application=application,
            update_package=True,
            slot=slot,
        )
    except release_artifacts.VerificationError as exc:
        raise PackageError(str(exc)) from exc

    return {
        "package": inspected,
        "verification": verified["verification"],
        "manifest": verified["manifest"],
        "vector": verified["vector"],
        "payload_end": verified["payload_end"],
    }


def flash_offset(address: int) -> int:
    if not (LAYOUT["flash_base"] <= address <= LAYOUT["flash_end"]):
        raise PackageError(f"address outside flash: 0x{address:08X}")
    return address - LAYOUT["flash_base"]


def metadata_record(
    *,
    sequence: int,
    state: int,
    active_slot: int,
    candidate_slot: int,
    image_version: int,
    confirmation_state: int,
) -> bytes:
    body = struct.pack(
        "<11I",
        METADATA_MAGIC,
        LAYOUT["boot_metadata_format_version"],
        LAYOUT["boot_metadata_record_size"],
        sequence,
        state,
        active_slot,
        candidate_slot,
        image_version,
        0,
        confirmation_state,
        0,
    )
    body += b"\x00" * 16
    crc = zlib.crc32(body) & 0xFFFFFFFF
    padding = b"\x00" * 56
    return body + struct.pack("<I", crc) + padding + struct.pack(
        "<II",
        METADATA_COMMIT0,
        METADATA_COMMIT1,
    )


def install_simulated(
    package: bytes,
    public_key: bytes,
    *,
    active_slot: str,
    active_version: int,
) -> tuple[bytes, dict[str, Any]]:
    if active_slot not in ("a", "b"):
        raise PackageError("active slot must be 'a' or 'b'")
    inactive_slot = "b" if active_slot == "a" else "a"
    active_slot_id = LAYOUT[f"slot_{active_slot}"]["id"]
    inactive_slot_id = LAYOUT[f"slot_{inactive_slot}"]["id"]

    verified = verify_package_bytes(package, public_key, slot=inactive_slot)
    image_version = verified["manifest"]["image_version"]
    if image_version <= active_version:
        raise PackageError("update package image version does not advance metadata version")

    flash = bytearray(b"\xFF" * LAYOUT["flash_total_size"])
    confirmed = metadata_record(
        sequence=1,
        state=STATE_CONFIRMED,
        active_slot=active_slot_id,
        candidate_slot=BOOT_SLOT_NONE,
        image_version=active_version,
        confirmation_state=1,
    )
    writing = metadata_record(
        sequence=2,
        state=STATE_WRITING,
        active_slot=active_slot_id,
        candidate_slot=inactive_slot_id,
        image_version=image_version,
        confirmation_state=0,
    )
    ready = metadata_record(
        sequence=3,
        state=STATE_CANDIDATE_READY,
        active_slot=active_slot_id,
        candidate_slot=inactive_slot_id,
        image_version=image_version,
        confirmation_state=0,
    )

    a_off = flash_offset(LAYOUT["boot_metadata_a_base"])
    b_off = flash_offset(LAYOUT["boot_metadata_b_base"])
    flash[a_off:a_off + len(confirmed)] = confirmed
    flash[b_off:b_off + len(writing)] = writing

    slot_base = LAYOUT[f"slot_{inactive_slot}_signed_image_base"]
    slot_off = flash_offset(slot_base)
    flash[slot_off:slot_off + len(package)] = package

    installed = bytes(flash[slot_off:slot_off + len(package)])
    installed_verified = verify_package_bytes(installed, public_key, slot=inactive_slot)
    if installed_verified["manifest"]["payload_sha512"] != verified["manifest"]["payload_sha512"]:
        raise PackageError("installed package hash changed")

    flash[a_off:a_off + len(ready)] = ready

    report = {
        "active_slot": active_slot,
        "inactive_slot": inactive_slot,
        "active_version": active_version,
        "candidate_version": image_version,
        "metadata_states": ["CONFIRMED", "WRITING", "CANDIDATE_READY"],
        "installed_address": slot_base,
        "installed_size": len(package),
        "verification": installed_verified["verification"],
    }
    return bytes(flash), report


def success_report(payload: dict[str, Any]) -> dict[str, Any]:
    return {"schema_version": 1, "result": "ok", **payload}


def failure_report(error: Exception) -> dict[str, Any]:
    return {"schema_version": 1, "result": "failed", "error": str(error)}


def run_build(args: argparse.Namespace) -> int:
    try:
        application = read_file(args.application, "application")
        seed = read_file(args.seed, "Ed25519 seed")
        package, payload_hash, initial_msp, reset_vector = signer.build_update_package(
            application,
            seed,
            slot=args.slot,
            image_version=args.image_version,
        )
        signer.write_output_atomically(args.output, package)
        report = success_report({
            "output": str(args.output),
            "slot": args.slot,
            "image_version": args.image_version,
            "package_size": len(package),
            "payload_size": len(application),
            "payload_sha512": payload_hash.hex(),
            "initial_msp": initial_msp,
            "reset_vector": reset_vector,
        })
        write_json(args.json_output, report)
        return EXIT_OK
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return EXIT_FAILED


def run_inspect(args: argparse.Namespace) -> int:
    try:
        report = success_report({
            "package": inspect_package_bytes(read_file(args.package, "package")),
        })
        write_json(args.json_output, report)
        return EXIT_OK
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return EXIT_FAILED


def run_verify(args: argparse.Namespace) -> int:
    try:
        application = read_file(args.application, "application") if args.application else None
        report = success_report(
            verify_package_bytes(
                read_file(args.package, "package"),
                public_key_from_args(args),
                slot=args.slot,
                application=application,
            )
        )
        write_json(args.json_output, report)
        return EXIT_OK
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return EXIT_FAILED


def run_install_sim(args: argparse.Namespace) -> int:
    try:
        flash, report_payload = install_simulated(
            read_file(args.package, "package"),
            public_key_from_args(args),
            active_slot=args.active_slot,
            active_version=args.active_version,
        )
        args.flash_output.parent.mkdir(parents=True, exist_ok=True)
        args.flash_output.write_bytes(flash)
        report = success_report({
            "flash_output": str(args.flash_output),
            **report_payload,
        })
        write_json(args.json_output, report)
        return EXIT_OK
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return EXIT_FAILED


def add_public_key_args(parser: argparse.ArgumentParser) -> None:
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--public-key-hex")
    group.add_argument("--public-key-header", type=Path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build and verify STM32 update packages")
    subparsers = parser.add_subparsers(dest="command", required=True)

    build = subparsers.add_parser("build", help="build an authenticated update package")
    build.add_argument("--application", required=True, type=Path)
    build.add_argument("--seed", required=True, type=Path)
    build.add_argument("--slot", required=True, choices=("a", "b"))
    build.add_argument("--output", required=True, type=Path)
    build.add_argument("--image-version", type=int, default=signer.IMAGE_VERSION)
    build.add_argument("--json-output", type=Path)
    build.set_defaults(func=run_build)

    inspect = subparsers.add_parser("inspect", help="inspect package metadata")
    inspect.add_argument("--package", required=True, type=Path)
    inspect.add_argument("--json-output", type=Path)
    inspect.set_defaults(func=run_inspect)

    verify = subparsers.add_parser("verify", help="verify an update package offline")
    verify.add_argument("--package", required=True, type=Path)
    verify.add_argument("--slot", required=True, choices=("a", "b"))
    verify.add_argument("--application", type=Path)
    verify.add_argument("--json-output", type=Path)
    add_public_key_args(verify)
    verify.set_defaults(func=run_verify)

    install = subparsers.add_parser("install-sim", help="install into simulated flash")
    install.add_argument("--package", required=True, type=Path)
    install.add_argument("--active-slot", required=True, choices=("a", "b"))
    install.add_argument("--active-version", type=int, default=signer.IMAGE_VERSION)
    install.add_argument("--flash-output", required=True, type=Path)
    install.add_argument("--json-output", type=Path)
    add_public_key_args(install)
    install.set_defaults(func=run_install_sim)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
