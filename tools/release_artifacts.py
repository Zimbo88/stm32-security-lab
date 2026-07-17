#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path
from typing import Any

from nacl.exceptions import BadSignatureError
from nacl.signing import VerifyKey


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from stm32f429_layout import LAYOUT  # noqa: E402


SIGNED_IMAGE_MAGIC = 0x31474953
SIGNED_HEADER_VERSION = 1
MIN_IMAGE_VERSION = 2
MANIFEST_STRUCT = struct.Struct("<8I64s")

EXIT_OK = 0
EXIT_VERIFY_FAILED = 1


class VerificationError(ValueError):
    pass


def repo_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_file(path: Path, label: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise VerificationError(f"failed to read {label} '{path}': {exc}") from exc


def sha512_hex(data: bytes) -> str:
    return hashlib.sha512(data).hexdigest()


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def artifact_record(path: Path) -> dict[str, Any]:
    data = read_file(path, "artifact")
    if len(data) == 0:
        raise VerificationError(f"artifact is empty: {path}")
    return {
        "path": repo_path(path),
        "size": len(data),
        "sha512": sha512_hex(data),
    }


def require_elf(path: Path) -> None:
    data = read_file(path, "ELF")
    if len(data) < 4 or data[:4] != b"\x7fELF":
        raise VerificationError(f"invalid ELF artifact: {path}")


def require_hex(path: Path) -> None:
    text = read_file(path, "HEX").decode("ascii", errors="strict")
    if not text.startswith(":"):
        raise VerificationError(f"invalid Intel HEX artifact: {path}")
    if ":00000001FF" not in text:
        raise VerificationError(f"Intel HEX EOF record missing: {path}")


def parse_define_u32(path: Path, name: str) -> int:
    text = path.read_text(encoding="ascii")
    match = re.search(rf"^\s*#define\s+{re.escape(name)}\s+([0-9A-Fa-fxXuUlL]+)", text, re.M)
    if match is None:
        raise VerificationError(f"missing {name} in {path}")
    return int(match.group(1).rstrip("uUlL"), 0)


def load_policy_versions() -> tuple[int, int]:
    policy = ROOT / "firmware" / "exp045_bootloader_v2" / "src" / "image_policy.h"
    header_version = parse_define_u32(policy, "SIGNED_HEADER_VERSION")
    rollback_version = parse_define_u32(policy, "MIN_IMAGE_VERSION")
    return header_version, rollback_version


def parse_public_key_header(path: Path) -> bytes:
    text = path.read_text(encoding="ascii")
    values = re.findall(r"0x([0-9A-Fa-f]{2})U?", text)
    if len(values) != 32:
        raise VerificationError(f"expected 32 public-key bytes in {path}, got {len(values)}")
    return bytes(int(value, 16) for value in values)


def public_key_from_args(args: argparse.Namespace) -> bytes:
    public_key_hex = getattr(args, "public_key_hex", None)
    public_key_header = getattr(args, "public_key_header", None)

    if bool(public_key_hex) == bool(public_key_header):
        raise VerificationError("provide exactly one public key source")

    if public_key_hex:
        try:
            key = bytes.fromhex(public_key_hex)
        except ValueError as exc:
            raise VerificationError("public key hex is not valid hexadecimal") from exc
        if len(key) != 32:
            raise VerificationError(f"public key must be 32 bytes, got {len(key)}")
        return key

    return parse_public_key_header(public_key_header)


def load_le32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def decode_manifest(manifest: bytes) -> dict[str, Any]:
    if len(manifest) != LAYOUT["signed_manifest_size"]:
        raise VerificationError("manifest has invalid size")

    (
        magic,
        header_version,
        image_version,
        vector_address,
        image_size,
        flags,
        reserved0,
        reserved1,
        payload_sha512,
    ) = MANIFEST_STRUCT.unpack(manifest)

    return {
        "magic": magic,
        "header_version": header_version,
        "image_version": image_version,
        "vector_address": vector_address,
        "image_size": image_size,
        "flags": flags,
        "reserved0": reserved0,
        "reserved1": reserved1,
        "payload_sha512": payload_sha512.hex(),
    }


def validate_vector_table(payload: bytes, payload_end: int) -> dict[str, int]:
    if len(payload) < LAYOUT["application_min_payload_size"]:
        raise VerificationError("payload is too small for vector table")

    initial_msp = load_le32(payload, 0)
    reset_vector = load_le32(payload, 4)
    reset_address = reset_vector & ~1

    if not (LAYOUT["application_msp_base"] < initial_msp <= LAYOUT["application_msp_end"]):
        raise VerificationError(f"initial MSP outside supported SRAM: 0x{initial_msp:08X}")

    if (initial_msp & (LAYOUT["application_msp_alignment"] - 1)) != 0:
        raise VerificationError(f"initial MSP is not {LAYOUT['application_msp_alignment']}-byte aligned")

    if (reset_vector & 1) == 0:
        raise VerificationError(f"reset vector does not set Thumb bit: 0x{reset_vector:08X}")

    if not (LAYOUT["application_base"] <= reset_address < payload_end):
        raise VerificationError(f"reset vector outside payload: 0x{reset_vector:08X}")

    return {
        "initial_msp": initial_msp,
        "reset_vector": reset_vector,
        "reset_address": reset_address,
    }


def verify_signed_image(
    signed_image: bytes,
    public_key: bytes,
    *,
    application: bytes | None = None,
) -> dict[str, Any]:
    manifest_size = LAYOUT["signed_manifest_size"]
    signature_size = LAYOUT["signed_signature_size"]
    header_size = LAYOUT["signed_image_header_size"]

    if len(signed_image) < header_size + LAYOUT["application_min_payload_size"]:
        raise VerificationError("signed image is too small")

    manifest_bytes = signed_image[:manifest_size]
    signature = signed_image[manifest_size:manifest_size + signature_size]
    padding = signed_image[manifest_size + signature_size:header_size]
    payload = signed_image[header_size:]
    manifest = decode_manifest(manifest_bytes)

    if manifest["magic"] != SIGNED_IMAGE_MAGIC:
        raise VerificationError("bad signed-image magic")
    if manifest["header_version"] != SIGNED_HEADER_VERSION:
        raise VerificationError("unsupported manifest header version")
    if manifest["image_version"] < MIN_IMAGE_VERSION:
        raise VerificationError("image version is below rollback floor")
    if manifest["flags"] & ~LAYOUT["signed_image_flags_allowed_mask"]:
        raise VerificationError("unsupported manifest flags")
    if manifest["reserved0"] != 0 or manifest["reserved1"] != 0:
        raise VerificationError("reserved manifest fields must be zero")
    if manifest["vector_address"] != LAYOUT["application_base"]:
        raise VerificationError("manifest vector address does not match application base")
    if manifest["image_size"] < LAYOUT["application_min_payload_size"]:
        raise VerificationError("payload size is below minimum")
    if manifest["image_size"] > LAYOUT["application_payload_max_size"]:
        raise VerificationError("payload size exceeds application region")
    if len(payload) != manifest["image_size"]:
        raise VerificationError("signed image length does not match manifest payload size")
    if any(byte != 0xFF for byte in padding):
        raise VerificationError("signed-image padding is not canonical 0xff")

    payload_end = manifest["vector_address"] + manifest["image_size"]
    if payload_end > LAYOUT["application_flash_end"]:
        raise VerificationError("payload extends beyond supported application flash")

    if application is not None and payload != application:
        raise VerificationError("signed-image payload does not match application binary")

    actual_payload_hash = hashlib.sha512(payload).digest()
    if actual_payload_hash.hex() != manifest["payload_sha512"]:
        raise VerificationError("payload SHA-512 does not match manifest")

    try:
        VerifyKey(public_key).verify(manifest_bytes, signature)
    except BadSignatureError as exc:
        raise VerificationError("Ed25519 signature verification failed") from exc

    vector = validate_vector_table(payload, payload_end)

    return {
        "manifest": manifest,
        "manifest_sha512": sha512_hex(manifest_bytes),
        "signature": signature.hex(),
        "signature_sha512": sha512_hex(signature),
        "payload_size": len(payload),
        "payload_sha512": actual_payload_hash.hex(),
        "payload_end": payload_end,
        "vector": vector,
        "verification": {
            "payload_hash_valid": True,
            "signature_valid": True,
            "application_payload_match": application is not None,
        },
    }


def git_output(args: list[str]) -> str | None:
    try:
        return subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
    except subprocess.CalledProcessError:
        return None


def git_info(args: argparse.Namespace | None = None) -> dict[str, Any]:
    if args is not None and args.git_commit:
        return {
            "commit": args.git_commit,
            "exact_tag": args.git_exact_tag,
            "dirty": args.git_dirty,
        }

    status = git_output(["status", "--porcelain", "--untracked-files=no"])
    return {
        "commit": git_output(["rev-parse", "HEAD"]),
        "exact_tag": git_output(["describe", "--exact-match", "--tags", "HEAD"]),
        "dirty": bool(status),
    }


def compiler_version(compiler: str) -> str:
    try:
        result = subprocess.run(
            [compiler, "--version"],
            check=True,
            text=True,
            capture_output=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise VerificationError(f"failed to query compiler '{compiler}': {exc}") from exc
    return result.stdout.splitlines()[0]


def read_make_assignments(path: Path) -> dict[str, str]:
    names = {"CC", "CPUFLAGS", "REPRO_FLAGS", "CFLAGS", "ASFLAGS", "LDFLAGS"}
    assignments: dict[str, str] = {}
    logical_lines: list[str] = []
    current = ""

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip()
        if line.endswith("\\"):
            current += line[:-1].strip() + " "
            continue
        logical_lines.append((current + line).strip())
        current = ""

    if current:
        logical_lines.append(current.strip())

    for line in logical_lines:
        match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*[:?+]?=\s*(.*)$", line)
        if match and match.group(1) in names:
            assignments[match.group(1)] = " ".join(match.group(2).split())

    return assignments


def validate_release_metadata(args: argparse.Namespace, signed_info: dict[str, Any]) -> None:
    expected_manifest = args.expected_manifest_version
    expected_application = args.expected_application_version
    expected_rollback = args.expected_rollback_version
    manifest = signed_info["manifest"]

    if manifest["header_version"] != expected_manifest:
        raise VerificationError(
            f"manifest version {manifest['header_version']} != expected {expected_manifest}"
        )
    if manifest["image_version"] != expected_application:
        raise VerificationError(
            f"application version {manifest['image_version']} != expected {expected_application}"
        )
    if manifest["image_version"] < expected_rollback:
        raise VerificationError(
            f"application version {manifest['image_version']} is below rollback floor {expected_rollback}"
        )
    if not args.bootloader_version:
        raise VerificationError("bootloader version must be nonempty")

    info = git_info(args)
    if args.require_clean and info["dirty"]:
        raise VerificationError("release requires a clean tracked working tree")
    if args.require_exact_tag and info["exact_tag"] is None:
        raise VerificationError("release requires HEAD to be exactly tagged")
    if args.require_exact_tag and args.release_version and info["exact_tag"] != args.release_version:
        raise VerificationError(
            f"git tag {info['exact_tag']} != release version {args.release_version}"
        )


def build_release_manifest(
    args: argparse.Namespace,
    signed_info: dict[str, Any],
    public_key: bytes,
) -> dict[str, Any]:
    bootloader_dir = ROOT / "firmware" / "exp045_bootloader_v2"
    application_dir = args.application_elf.resolve().parents[1]

    return {
        "schema_version": 1,
        "release_version": args.release_version,
        "git": git_info(args),
        "toolchain": {
            "compiler": args.compiler,
            "compiler_version": compiler_version(args.compiler),
        },
        "build_options": {
            "bootloader": read_make_assignments(bootloader_dir / "Makefile"),
            "application": read_make_assignments(application_dir / "Makefile"),
        },
        "versions": {
            "bootloader": args.bootloader_version,
            "application": signed_info["manifest"]["image_version"],
            "manifest": signed_info["manifest"]["header_version"],
            "rollback_minimum": args.expected_rollback_version,
        },
        "public_key": {
            "fingerprint_sha256": sha256_hex(public_key),
        },
        "artifacts": {
            "bootloader": {
                "elf": artifact_record(args.bootloader_elf),
                "bin": artifact_record(args.bootloader_bin),
                "hex": artifact_record(args.bootloader_hex),
            },
            "application": {
                "name": args.application_name,
                "elf": artifact_record(args.application_elf),
                "bin": artifact_record(args.application_bin),
                "hex": artifact_record(args.application_hex),
            },
            "signed_image": artifact_record(args.signed_image),
            "manifest": {
                "size": LAYOUT["signed_manifest_size"],
                "sha512": signed_info["manifest_sha512"],
                "fields": signed_info["manifest"],
            },
            "signature": {
                "algorithm": "Ed25519",
                "size": LAYOUT["signed_signature_size"],
                "sha512": signed_info["signature_sha512"],
                "value": signed_info["signature"],
            },
            "payload": {
                "size": signed_info["payload_size"],
                "sha512": signed_info["payload_sha512"],
                "vector": signed_info["vector"],
            },
        },
        "verification": signed_info["verification"],
    }


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="ascii")


def success_report(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "result": "ok",
        **payload,
    }


def failure_report(error: Exception) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "result": "failed",
        "error": str(error),
    }


def emit(data: dict[str, Any], output: Path | None) -> None:
    text = json.dumps(data, indent=2, sort_keys=True) + "\n"
    if output is None:
        sys.stdout.write(text)
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="ascii")


def run_verify_signed(args: argparse.Namespace) -> int:
    try:
        public_key = public_key_from_args(args)
        application = read_file(args.application, "application") if args.application else None
        signed_info = verify_signed_image(
            read_file(args.signed_image, "signed image"),
            public_key,
            application=application,
        )
        report = success_report({
            "public_key": {"fingerprint_sha256": sha256_hex(public_key)},
            "signed_image": {
                "path": repo_path(args.signed_image),
                "size": args.signed_image.stat().st_size,
            },
            **signed_info,
        })
        emit(report, args.json_output)
        return EXIT_OK
    except Exception as exc:
        emit(failure_report(exc), args.json_output)
        return EXIT_VERIFY_FAILED


def run_verify_release(args: argparse.Namespace) -> int:
    try:
        require_elf(args.bootloader_elf)
        require_elf(args.application_elf)
        require_hex(args.bootloader_hex)
        require_hex(args.application_hex)

        public_key = public_key_from_args(args)
        signed_info = verify_signed_image(
            read_file(args.signed_image, "signed image"),
            public_key,
            application=read_file(args.application_bin, "application binary"),
        )
        validate_release_metadata(args, signed_info)

        manifest = build_release_manifest(args, signed_info, public_key)
        report = success_report({
            "manifest_path": repo_path(args.manifest_output),
            "release_manifest_sha512": sha512_hex(
                (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("ascii")
            ),
            "verification": manifest["verification"],
        })
        write_json(args.manifest_output, manifest)
        emit(report, args.report_output)
        return EXIT_OK
    except Exception as exc:
        emit(failure_report(exc), args.report_output)
        return EXIT_VERIFY_FAILED


def add_public_key_args(parser: argparse.ArgumentParser) -> None:
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--public-key-hex")
    group.add_argument("--public-key-header", type=Path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Verify STM32 secure-boot release artifacts")
    subparsers = parser.add_subparsers(dest="command", required=True)

    signed = subparsers.add_parser("verify-signed", help="verify one signed image")
    signed.add_argument("--signed-image", required=True, type=Path)
    signed.add_argument("--application", type=Path)
    signed.add_argument("--json-output", type=Path)
    add_public_key_args(signed)
    signed.set_defaults(func=run_verify_signed)

    release = subparsers.add_parser("verify-release", help="verify complete release artifacts")
    release.add_argument("--bootloader-elf", required=True, type=Path)
    release.add_argument("--bootloader-bin", required=True, type=Path)
    release.add_argument("--bootloader-hex", required=True, type=Path)
    release.add_argument("--application-elf", required=True, type=Path)
    release.add_argument("--application-bin", required=True, type=Path)
    release.add_argument("--application-hex", required=True, type=Path)
    release.add_argument("--signed-image", required=True, type=Path)
    release.add_argument("--manifest-output", required=True, type=Path)
    release.add_argument("--report-output", type=Path)
    release.add_argument("--application-name", default="exp066_research_platform_core")
    release.add_argument("--bootloader-version", default="exp045")
    release.add_argument("--release-version")
    release.add_argument("--compiler", default="arm-none-eabi-gcc")
    release.add_argument("--git-commit")
    release.add_argument("--git-exact-tag")
    release.add_argument("--git-dirty", action="store_true")
    header_version, rollback_version = load_policy_versions()
    release.add_argument("--expected-manifest-version", type=int, default=header_version)
    release.add_argument("--expected-application-version", type=int, default=rollback_version)
    release.add_argument("--expected-rollback-version", type=int, default=rollback_version)
    release.add_argument("--require-clean", action="store_true")
    release.add_argument("--require-exact-tag", action="store_true")
    add_public_key_args(release)
    release.set_defaults(func=run_verify_release)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
