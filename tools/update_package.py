#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
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

METADATA_TOOL = ROOT / "tools" / "build" / "boot_metadata_provision.bin"
spec = importlib.util.spec_from_file_location("build_signed_image", SIGNER_PATH)
if spec is None or spec.loader is None:
    raise RuntimeError("failed to load signed-image builder")
signer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(signer)


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


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def git_commit() -> str | None:
    commit = release_artifacts.git_output(["rev-parse", "HEAD"])
    if commit is None:
        return None
    if len(commit) != 40 or any(c not in "0123456789abcdef" for c in commit.lower()):
        return None
    status = release_artifacts.git_output(
        ["status", "--porcelain", "--untracked-files=no"]
    )
    if status is None or status:
        return None
    return commit


def public_key_from_args(args: argparse.Namespace) -> bytes:
    try:
        return release_artifacts.public_key_from_args(args)
    except release_artifacts.VerificationError as exc:
        raise PackageError(str(exc)) from exc


def public_key_report_from_args(
    args: argparse.Namespace
) -> tuple[bytes, dict[str, Any]]:
    public_key_hex = getattr(args, "public_key_hex", None)
    public_key_header = getattr(args, "public_key_header", None)

    if public_key_hex is not None:
        key = public_key_from_args(args)
        return key, {
            "public_key_source": "public-key-hex",
            "public_key_hex": key.hex(),
            "public_key_sha256": sha256_hex(key),
        }

    if public_key_header is None:
        raise PackageError("public key header path is unavailable")

    header_bytes = read_file(public_key_header, "public key header")
    key = public_key_from_args(args)
    return key, {
        "public_key_source": "public-key-header",
        "public_key_header_path": release_artifacts.repo_path(public_key_header),
        "public_key_header_sha256": sha256_hex(header_bytes),
        "public_key_hex": key.hex(),
        "public_key_sha256": sha256_hex(key),
    }


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


def ensure_metadata_tool() -> Path:
    if METADATA_TOOL.exists():
        return METADATA_TOOL

    try:
        subprocess.run(
            ["make", "-C", str(ROOT / "tools"), "boot-metadata-provision"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = (
            exc.stderr.strip()
            if isinstance(exc, subprocess.CalledProcessError)
            else str(exc)
        )
        raise PackageError(
            f"failed to build boot metadata provisioning tool: {detail}"
        ) from exc

    if not METADATA_TOOL.exists():
        raise PackageError("boot metadata provisioning tool was not produced")
    return METADATA_TOOL


def metadata_update_sequence(
    *,
    active_slot: str,
    active_version: int,
    candidate_slot: str,
    candidate_version: int,
) -> tuple[bytes, bytes, bytes]:
    tool = ensure_metadata_tool()

    with tempfile.TemporaryDirectory(prefix="boot_metadata_") as tmp:
        tmpdir = Path(tmp)
        confirmed = tmpdir / "confirmed.bin"
        writing = tmpdir / "writing.bin"
        ready = tmpdir / "candidate_ready.bin"
        command = [
            str(tool),
            "create-update-sequence",
            "--active-slot",
            active_slot,
            "--active-version",
            str(active_version),
            "--candidate-slot",
            candidate_slot,
            "--candidate-version",
            str(candidate_version),
            "--confirmed-output",
            str(confirmed),
            "--writing-output",
            str(writing),
            "--candidate-ready-output",
            str(ready),
        ]
        try:
            subprocess.run(
                command,
                check=True,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            detail = (
                exc.stderr.strip()
                if isinstance(exc, subprocess.CalledProcessError)
                else str(exc)
            )
            raise PackageError(
                f"failed to create metadata update sequence: {detail}"
            ) from exc

        records = (confirmed.read_bytes(), writing.read_bytes(), ready.read_bytes())

    for record in records:
        if len(record) != LAYOUT["boot_metadata_record_size"]:
            raise PackageError("metadata provisioning tool produced an invalid record size")
    return records


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

    verified = verify_package_bytes(package, public_key, slot=inactive_slot)
    image_version = verified["manifest"]["image_version"]
    if image_version <= active_version:
        raise PackageError("update package image version does not advance metadata version")

    flash = bytearray(b"\xFF" * LAYOUT["flash_total_size"])
    confirmed, writing, ready = metadata_update_sequence(
        active_slot=active_slot,
        active_version=active_version,
        candidate_slot=inactive_slot,
        candidate_version=image_version,
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
        "target_layout": release_artifacts.layout_report(),
        "metadata_states": ["CONFIRMED", "WRITING", "CANDIDATE_READY"],
        "installed_address": slot_base,
        "installed_size": len(package),
        "verification": installed_verified["verification"],
    }
    return bytes(flash), report


def success_report(payload: dict[str, Any]) -> dict[str, Any]:
    return {"schema_version": 1, "result": "ok", **payload}


def failure_report(
    error: Exception,
    context: dict[str, Any] | None = None
) -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema_version": 1,
        "result": "failed",
        "error": str(error),
    }
    if context is not None:
        report.update(context)
    return report


def verify_report_context(
    args: argparse.Namespace,
    *,
    package: bytes | None,
    application: bytes | None,
    public_key: bytes | None,
    public_key_report: dict[str, Any] | None,
) -> dict[str, Any]:
    manifest_size = LAYOUT["signed_manifest_size"]
    context: dict[str, Any] = {
        "verification_timestamp_utc": utc_timestamp(),
        "package_path": release_artifacts.repo_path(args.package),
        "package_sha256": sha256_hex(package) if package is not None else None,
        "package_size": len(package) if package is not None else None,
        "public_key_source": None,
        "public_key_hex": public_key.hex() if public_key is not None else None,
        "public_key_sha256": sha256_hex(public_key) if public_key is not None else None,
        "signed_region_offset": 0,
        "signed_region_size": manifest_size,
        "signed_region_description": "Ed25519 signature over serialized manifest bytes only",
        "layout_profile": LAYOUT["profile"],
        "target": LAYOUT["target"],
        "tool_path": release_artifacts.repo_path(Path(__file__)),
        "git_commit": git_commit(),
    }

    if args.application is not None:
        context["application_path"] = release_artifacts.repo_path(args.application)
        context["application_sha256"] = (
            sha256_hex(application) if application is not None else None
        )

    if public_key_report is not None:
        context.update(public_key_report)
    elif getattr(args, "public_key_hex", None) is not None:
        context["public_key_source"] = "public-key-hex"
    elif getattr(args, "public_key_header", None) is not None:
        context["public_key_source"] = "public-key-header"
        context["public_key_header_path"] = release_artifacts.repo_path(
            args.public_key_header
        )

    if package is not None:
        try:
            inspected = inspect_package_bytes(package)
            context["package"] = inspected
            context["slot"] = inspected["slot"]
            context["image_version"] = inspected["image_version"]
            context["payload_sha512"] = inspected["payload_sha512"]
        except Exception as exc:
            context["package_inspect_error"] = str(exc)

    return context


def verify_success_report(
    args: argparse.Namespace,
    *,
    package: bytes,
    application: bytes | None,
    public_key: bytes,
    public_key_report: dict[str, Any],
    verified: dict[str, Any],
) -> dict[str, Any]:
    context = verify_report_context(
        args,
        package=package,
        application=application,
        public_key=public_key,
        public_key_report=public_key_report,
    )
    verification = verified["verification"]
    package_report = verified["package"]
    context.update({
        "slot": package_report["slot"],
        "image_version": package_report["image_version"],
        "payload_sha512": package_report["payload_sha512"],
        "signature_valid": verification["signature_valid"],
        "payload_hash_valid": verification["payload_hash_valid"],
        "application_payload_match": verification["application_payload_match"],
        "target_layout": release_artifacts.layout_report(),
        **verified,
    })
    return success_report(context)


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
            "target_layout": release_artifacts.layout_report(),
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
            "target_layout": release_artifacts.layout_report(),
            "package": inspect_package_bytes(read_file(args.package, "package")),
        })
        write_json(args.json_output, report)
        return EXIT_OK
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return EXIT_FAILED


def run_verify(args: argparse.Namespace) -> int:
    package: bytes | None = None
    application: bytes | None = None
    public_key: bytes | None = None
    public_key_report: dict[str, Any] | None = None
    try:
        package = read_file(args.package, "package")
        application = read_file(args.application, "application") if args.application else None
        public_key, public_key_report = public_key_report_from_args(args)
        verified = verify_package_bytes(
            package,
            public_key,
            slot=args.slot,
            application=application,
        )
        report = verify_success_report(
            args,
            package=package,
            application=application,
            public_key=public_key,
            public_key_report=public_key_report,
            verified=verified,
        )
        write_json(args.json_output, report)
        return EXIT_OK
    except Exception as exc:
        if package is None:
            try:
                package = read_file(args.package, "package")
            except Exception:
                package = None
        if application is None and args.application is not None:
            try:
                application = read_file(args.application, "application")
            except Exception:
                application = None
        if public_key_report is None:
            try:
                public_key, public_key_report = public_key_report_from_args(args)
            except Exception:
                public_key = None
                public_key_report = None
        context = verify_report_context(
            args,
            package=package,
            application=application,
            public_key=public_key,
            public_key_report=public_key_report,
        )
        write_json(args.json_output, failure_report(exc, context))
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
