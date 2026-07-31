#!/usr/bin/env python3
"""Create or verify a non-secret pre-RDP2 research baseline.

The command only builds firmware, reads local artifacts, and writes an
ignored baseline directory.  It never invokes a programmer, touches option
bytes, connects to a target, or copies a signing key.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "baseline" / "rdp2-final"
DEFAULT_SPEC = ROOT / "config" / "rdp2_research_markers.json"
PUBLIC_KEY_HEADER = ROOT / "firmware" / "exp045_bootloader_v2" / "src" / "firmware_public_key.h"

sys.path.insert(0, str(ROOT / "tools"))

import rdp2_marker  # noqa: E402
import release_artifacts  # noqa: E402
from stm32f429_layout import LAYOUT  # noqa: E402
from update_package import inspect_package_bytes  # noqa: E402


class BaselineError(ValueError):
    """Raised when baseline inputs are incomplete or inconsistent."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_quiet(command: list[str]) -> None:
    try:
        subprocess.run(
            command,
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise BaselineError(f"build command failed: {command[0]}") from exc


def git_value(*args: str) -> str | None:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip()


def toolchain_version() -> str | None:
    compiler = shutil.which("arm-none-eabi-gcc")
    if compiler is None:
        return None
    try:
        result = subprocess.run(
            [compiler, "--version"], check=True, capture_output=True, text=True
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    return result.stdout.splitlines()[0] if result.stdout.splitlines() else None


def build_artifacts(signing_seed: Path | None) -> None:
    run_quiet([
        "make", "-C", "firmware/exp045_bootloader_v2", "clean", "all",
        "LAYOUT_PROFILE=stm32f429_1m",
    ])
    if signing_seed is None:
        raise BaselineError("--build requires --signing-seed; the seed is never copied")
    if not signing_seed.is_file():
        raise BaselineError("--signing-seed does not name a readable file")
    run_quiet([
        "make", "-C", "firmware/exp066_research_platform_core",
        "slot-releases", "LAYOUT_PROFILE=stm32f429_1m",
        f"SIGNING_SEED={signing_seed}",
        f"PUBLIC_KEY_HEADER={PUBLIC_KEY_HEADER}",
    ])


def artifact_sources() -> dict[str, Path]:
    return {
        "bootloader.elf": ROOT / "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.elf",
        "bootloader.bin": ROOT / "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin",
        "bootloader.hex": ROOT / "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.hex",
        "slot-a.elf": ROOT / "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a.elf",
        "slot-a.bin": ROOT / "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a.bin",
        "slot-a.hex": ROOT / "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a.hex",
        "slot-a.update.bin": ROOT / "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_update_v2.bin",
        "slot-a.package-build.json": ROOT / "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_package_build.json",
        "slot-a.release-manifest.json": ROOT / "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_release_manifest.json",
        "slot-a.package-verify.json": ROOT / "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_package_verify.json",
        "slot-b.elf": ROOT / "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b.elf",
        "slot-b.bin": ROOT / "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b.bin",
        "slot-b.hex": ROOT / "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b.hex",
        "slot-b.update.bin": ROOT / "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_update_v2.bin",
        "slot-b.package-build.json": ROOT / "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_package_build.json",
        "slot-b.release-manifest.json": ROOT / "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_release_manifest.json",
        "slot-b.package-verify.json": ROOT / "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_package_verify.json",
    }


def require_sources(sources: dict[str, Path]) -> None:
    missing = [f"{name}: {path}" for name, path in sources.items() if not path.is_file()]
    if missing:
        raise BaselineError("missing reference artifacts:\n" + "\n".join(missing))


def package_report(path: Path) -> dict[str, Any]:
    try:
        return inspect_package_bytes(path.read_bytes())
    except Exception as exc:
        raise BaselineError(f"update package verification failed for {path.name}") from exc


def build_manifest(
    *,
    output: Path,
    sources: dict[str, Path],
    marker_report: dict[str, Any],
) -> dict[str, Any]:
    public_key = release_artifacts.parse_public_key_header(PUBLIC_KEY_HEADER)
    artifact_records: dict[str, Any] = {}
    for name, source in sources.items():
        artifact_records[name] = {
            "source": str(source.relative_to(ROOT)),
            "sha256": sha256_file(source),
            "size": source.stat().st_size,
        }

    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if epoch is not None:
        try:
            build_time = datetime.fromtimestamp(int(epoch), tz=timezone.utc).isoformat()
        except (ValueError, OverflowError, OSError):
            build_time = None
    else:
        build_time = None

    return {
        "schema_version": 1,
        "purpose": "Pre-RDP2 software baseline; no RDP or option-byte operation.",
        "git": {
            "commit": git_value("rev-parse", "HEAD"),
            "dirty": bool(git_value("status", "--porcelain", "--untracked-files=no")),
        },
        "toolchain": {
            "compiler": "arm-none-eabi-gcc",
            "version": toolchain_version(),
        },
        "build": {
            "source_date_epoch": epoch,
            "build_timestamp_utc": build_time,
            "embedded_build_id": None,
            "build_id_note": "All firmware linkers use --build-id=none; artifact hashes are the build identity.",
        },
        "target": {
            "mcu": "STM32F429IGT6",
            "layout_profile": LAYOUT["profile"],
            "layout": release_artifacts.layout_report(),
        },
        "versions": {
            "bootloader": "exp045",
            "slot_a": package_report(sources["slot-a.update.bin"])["image_version"],
            "slot_b": package_report(sources["slot-b.update.bin"])["image_version"],
            "manifest": package_report(sources["slot-a.update.bin"])["format_version"],
            "rollback_floor": 2,
        },
        "public_key": {
            "source": str(PUBLIC_KEY_HEADER.relative_to(ROOT)),
            "fingerprint_sha256": sha256_hex(public_key),
        },
        "artifacts": artifact_records,
        "markers": marker_report,
        "expected_uart_start_output": [
            "STM32F429 SECURITY LAB",
            "EXP045 BOOTLOADER V2",
            "Slot policy      = OK",
            "Signature and payload hash accepted.",
            "EXP066 RESEARCH PLATFORM",
        ],
        "expected_slot_metadata": {
            "factory": {
                "state": "CONFIRMED",
                "active_slot": "A",
                "candidate_slot": "NONE",
                "boot_attempt_count": 0,
                "confirmation_state": 1,
            },
            "update_sequence": ["CONFIRMED", "WRITING", "CANDIDATE_READY", "PENDING_TRIAL", "CONFIRMED"],
        },
        "option_bytes": {
            "value": None,
            "operator_record_required": True,
            "note": "Read-only manual record required; this script never reads or writes option bytes.",
        },
        "private_signing_key": {
            "copied": False,
            "printed": False,
            "operator_managed_external_backup_required": True,
        },
    }


def copy_and_hash_artifacts(output: Path, sources: dict[str, Path]) -> dict[str, str]:
    artifact_dir = output / "artifacts"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    hashes: dict[str, str] = {}
    for name, source in sources.items():
        destination = artifact_dir / name
        shutil.copyfile(source, destination)
        hashes[f"artifacts/{name}"] = sha256_file(destination)
    return hashes


def write_symbol_tables(output: Path, sources: dict[str, Path]) -> None:
    symbol_dir = output / "elf-symbols"
    symbol_dir.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        if source.suffix != ".elf":
            continue
        symbols = rdp2_marker.defined_symbols(source)
        text = "".join(f"{address:08x} {symbol}\n" for symbol, address in sorted(symbols.items(), key=lambda item: item[1]))
        (symbol_dir / f"{name}.txt").write_text(text, encoding="ascii")


def create_baseline(output: Path, *, build: bool, signing_seed: Path | None) -> None:
    if build:
        if signing_seed is not None:
            signing_seed = signing_seed.resolve()
        build_artifacts(signing_seed)
    sources = artifact_sources()
    require_sources(sources)
    spec = rdp2_marker.load_spec(DEFAULT_SPEC)
    marker_report = rdp2_marker.inspect_spec(spec)

    output.mkdir(parents=True, exist_ok=True)
    hashes = copy_and_hash_artifacts(output, sources)
    write_symbol_tables(output, sources)
    (output / "marker-report.json").write_text(
        json.dumps(marker_report, indent=2, sort_keys=True) + "\n", encoding="ascii"
    )
    (output / "memory-layout.json").write_text(
        json.dumps(release_artifacts.layout_report(), indent=2, sort_keys=True) + "\n",
        encoding="ascii",
    )
    manifest = build_manifest(output=output, sources=sources, marker_report=marker_report)
    (output / "release-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="ascii"
    )
    (output / "SHA256SUMS").write_text(
        "".join(f"{digest}  {name}\n" for name, digest in sorted(hashes.items())),
        encoding="ascii",
    )
    (output / "README.txt").write_text(
        "This ignored directory is a local pre-RDP2 baseline. It may contain public firmware artifacts and local hashes, but never a private signing key, hardware dump, or option-byte write command.\n",
        encoding="ascii",
    )


def verify_baseline(output: Path) -> None:
    manifest_path = output / "release-manifest.json"
    marker_path = output / "marker-report.json"
    if not manifest_path.is_file() or not marker_path.is_file():
        raise BaselineError(f"baseline is incomplete: {output}")
    manifest = json.loads(manifest_path.read_text(encoding="ascii"))
    hashes_path = output / "SHA256SUMS"
    expected_hashes: dict[str, str] = {}
    for line in hashes_path.read_text(encoding="ascii").splitlines():
        digest, name = line.split("  ", 1)
        expected_hashes[name] = digest
    actual_hashes = {
        name: sha256_file(output / name)
        for name in expected_hashes
        if (output / name).is_file()
    }
    if actual_hashes != expected_hashes:
        raise BaselineError("baseline SHA256SUMS verification failed")
    if manifest.get("private_signing_key", {}).get("copied") is not False:
        raise BaselineError("baseline manifest indicates private-key copying")
    marker_report = json.loads(marker_path.read_text(encoding="ascii"))
    if marker_report.get("all_verified") is not True:
        raise BaselineError("baseline marker verification is not complete")
    print(json.dumps({"result": "ok", "baseline": str(output), "artifacts": len(actual_hashes)}, indent=2))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create", help="build/copy and record a local baseline")
    create.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    create.add_argument("--build", action="store_true", help="clean-build bootloader and both signed slots")
    create.add_argument("--signing-seed", type=Path, help="external seed path; never copied or printed")
    verify = subparsers.add_parser("verify", help="verify a previously created baseline")
    verify.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "create":
            create_baseline(args.output.resolve(), build=args.build, signing_seed=args.signing_seed)
            print(json.dumps({"result": "ok", "baseline": str(args.output.resolve())}, indent=2))
        else:
            verify_baseline(args.output.resolve())
        return 0
    except (BaselineError, OSError, KeyError, ValueError) as exc:
        print(json.dumps({"result": "failed", "error": str(exc)}, indent=2), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
