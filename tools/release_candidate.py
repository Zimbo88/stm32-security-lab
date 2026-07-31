#!/usr/bin/env python3
"""Build a local, deterministic, non-publishing release candidate.

The command never flashes hardware, writes option bytes, creates a tag, or
pushes anything.  A test key is intentionally supported for CI-like builds;
such a candidate is not hardware-installable unless its public key matches
the key embedded in Stage 0.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import stat
import subprocess  # nosec B404 - argv is constructed internally and shell is disabled
import sys
import tarfile
import tempfile
from pathlib import Path

from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[1]
LAYOUT_PROFILE = "stm32f429_1m"
IMAGE_VERSION = 2
TEST_SEED = bytes(range(32))
BOOTLOADER_BUILD = ROOT / "firmware/exp045_bootloader_v2/build"
LEGACY_BUILD = ROOT / "firmware/exp065_signed_app/build"
SLOT_BUILD = ROOT / "firmware/exp066_research_platform_core/build"


class CandidateError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    result = subprocess.run(  # nosec B603 B607 - fixed git argv and repository cwd
        ["git", *args], cwd=ROOT, check=True, capture_output=True, text=True
    )
    return result.stdout.strip()


def run(command: list[str]) -> None:
    print("$ " + " ".join(command))
    try:
        subprocess.run(command, cwd=ROOT, check=True)  # nosec B603 - shell is disabled
    except (OSError, subprocess.CalledProcessError) as exc:
        raise CandidateError(f"command failed: {command[0]}") from exc


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="ascii")


def copy(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise CandidateError(f"missing build artifact: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def copy_report(source: Path, destination: Path) -> None:
    """Copy a report while normalizing its known verification timestamp."""
    if source.name.endswith("_package_verify.json"):
        value = json.loads(source.read_text(encoding="ascii"))
        if isinstance(value, dict) and "verification_timestamp_utc" in value:
            value["verification_timestamp_utc"] = "<normalized>"
            write_json(destination, value)
            return
    copy(source, destination)


def read_embedded_key() -> bytes:
    import re

    header = ROOT / "firmware/exp045_bootloader_v2/src/firmware_public_key.h"
    values = re.findall(r"0x([0-9A-Fa-f]{2})U?", header.read_text(encoding="ascii"))
    if len(values) != 32:
        raise CandidateError("embedded public key does not contain 32 bytes")
    return bytes(int(value, 16) for value in values)


def git_state() -> tuple[str, bool]:
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain"))
    return commit, dirty


def make_seed(args: argparse.Namespace, tempdir: Path) -> tuple[Path, bytes, str]:
    if args.test_key and args.signing_seed:
        raise CandidateError("choose --test-key or --signing-seed, not both")
    if args.test_key:
        seed_path = tempdir / "synthetic-test-seed.bin"
        seed_path.write_bytes(TEST_SEED)
        seed_path.chmod(stat.S_IRUSR | stat.S_IWUSR)
        return seed_path, TEST_SEED, "CI_TEST_KEY_NOT_FOR_PRODUCTION"
    if args.signing_seed is None:
        raise CandidateError("provide --test-key or an external --signing-seed")
    seed_path = args.signing_seed.expanduser().resolve()
    seed = seed_path.read_bytes()
    if len(seed) != 32:
        raise CandidateError("Ed25519 signing seed must be exactly 32 bytes")
    return seed_path, seed, "EXTERNAL_OPERATOR_KEY"


def build_firmware(seed_path: Path, public_key_hex: str, release_version: str) -> None:
    run(["make", "-C", "firmware/exp045_bootloader_v2", "clean", "all", "report", f"LAYOUT_PROFILE={LAYOUT_PROFILE}"])
    run(["make", "-C", "firmware/exp065_signed_app", "clean", "all", f"LAYOUT_PROFILE={LAYOUT_PROFILE}"])
    run([
        "make", "-C", "firmware/exp065_signed_app", "signed",
        f"LAYOUT_PROFILE={LAYOUT_PROFILE}", f"SIGNING_SEED={seed_path}",
    ])
    run([
        "python3", "tools/release_artifacts.py", "verify-release",
        "--bootloader-elf", str(BOOTLOADER_BUILD / "exp045_bootloader_v2.elf"),
        "--bootloader-bin", str(BOOTLOADER_BUILD / "exp045_bootloader_v2.bin"),
        "--bootloader-hex", str(BOOTLOADER_BUILD / "exp045_bootloader_v2.hex"),
        "--application-elf", str(LEGACY_BUILD / "exp065_signed_app.elf"),
        "--application-bin", str(LEGACY_BUILD / "exp065_signed_app.bin"),
        "--application-hex", str(LEGACY_BUILD / "exp065_signed_app.hex"),
        "--signed-image", str(LEGACY_BUILD / "exp065_signed_app_signed.bin"),
        "--public-key-hex", public_key_hex,
        "--manifest-output", str(LEGACY_BUILD / "exp065_release_manifest.json"),
        "--report-output", str(LEGACY_BUILD / "exp065_release_verification.json"),
        "--application-name", "exp065_signed_app",
        "--bootloader-version", "exp045",
        "--release-version", release_version,
        "--git-commit", git("rev-parse", "HEAD"),
    ])
    run([
        "make", "-C", "firmware/exp066_research_platform_core", "slot-releases",
        f"LAYOUT_PROFILE={LAYOUT_PROFILE}", f"SIGNING_SEED={seed_path}",
        f"PUBLIC_KEY_HEX={public_key_hex}", f"IMAGE_VERSION={IMAGE_VERSION}",
    ])


def stage_artifacts(output: Path) -> list[Path]:
    mapping = {
        "bootloader/bootloader.elf": BOOTLOADER_BUILD / "exp045_bootloader_v2.elf",
        "bootloader/bootloader.bin": BOOTLOADER_BUILD / "exp045_bootloader_v2.bin",
        "bootloader/bootloader.hex": BOOTLOADER_BUILD / "exp045_bootloader_v2.hex",
        "legacy/legacy-app.elf": LEGACY_BUILD / "exp065_signed_app.elf",
        "legacy/legacy-app.bin": LEGACY_BUILD / "exp065_signed_app.bin",
        "legacy/legacy-app.hex": LEGACY_BUILD / "exp065_signed_app.hex",
        "legacy/signed-image.bin": LEGACY_BUILD / "exp065_signed_app_signed.bin",
        "legacy/release-manifest.json": LEGACY_BUILD / "exp065_release_manifest.json",
        "legacy/verification.json": LEGACY_BUILD / "exp065_release_verification.json",
    }
    for slot in ("a", "b"):
        source = SLOT_BUILD / f"slot_{slot}"
        prefix = f"exp066_research_platform_core_slot_{slot}"
        mapping.update({
            f"slot-{slot}/slot-{slot}.elf": source / f"{prefix}.elf",
            f"slot-{slot}/slot-{slot}.bin": source / f"{prefix}.bin",
            f"slot-{slot}/slot-{slot}.hex": source / f"{prefix}.hex",
            f"slot-{slot}/update-package.bin": source / f"{prefix}_slot_{slot}_update_v2.bin",
            f"slot-{slot}/package-build.json": source / f"{prefix}_slot_{slot}_package_build.json",
            f"slot-{slot}/release-manifest.json": source / f"{prefix}_slot_{slot}_release_manifest.json",
            f"slot-{slot}/package-verify.json": source / f"{prefix}_slot_{slot}_package_verify.json",
        })
    staged: list[Path] = []
    for relative, source in mapping.items():
        destination = output / relative
        copy_report(source, destination)
        staged.append(destination)
    return staged


def deterministic_archive(source: Path, archive: Path) -> None:
    archive.parent.mkdir(parents=True, exist_ok=True)
    with archive.open("wb") as stream:
        import gzip

        with gzip.GzipFile(fileobj=stream, mode="wb", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as tar:
                for path in sorted(source.rglob("*")):
                    relative = path.relative_to(source).as_posix()
                    info = tar.gettarinfo(path, arcname=relative)
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    info.mtime = 0
                    if path.is_file():
                        with path.open("rb") as item:
                            tar.addfile(info, item)
                    else:
                        tar.addfile(info)


def create_candidate(args: argparse.Namespace) -> None:
    commit, dirty = git_state()
    if dirty and not args.allow_dirty:
        raise CandidateError("working tree is dirty; commit changes or use --allow-dirty")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise CandidateError(f"output already exists and is non-empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    embedded_key = read_embedded_key()
    with tempfile.TemporaryDirectory(prefix="stm32_release_seed_") as temp:
        seed_path, seed, key_mode = make_seed(args, Path(temp))
        public_key = bytes(SigningKey(seed).verify_key)
        public_key_hex = public_key.hex()
        build_firmware(seed_path, public_key_hex, args.release_version)
        staged = stage_artifacts(output)
        marker_path = output / "marker-report.json"
        run(["python3", "tools/rdp2_marker.py", "inspect", "--output", str(marker_path)])
        write_json(output / "memory-layout.json", {
            "schema_version": 1,
            "layout_profile": LAYOUT_PROFILE,
            "mcu": "STM32F429IGT6",
            "flash_size": 1024 * 1024,
            "sram_size": 256 * 1024,
        })
        (output / "public-key.hex").write_text(public_key_hex + "\n", encoding="ascii")
        (output / "embedded-public-key.hex").write_text(embedded_key.hex() + "\n", encoding="ascii")
        run([
            "python3", "tools/generate_sbom.py", "--output", str(output / "sbom.spdx.json"),
            "--release-version", args.release_version, "--source-commit", commit,
        ])
        copy(ROOT / "THIRD_PARTY_NOTICES.md", output / "THIRD_PARTY_NOTICES.md")
        copy(ROOT / "CHANGELOG.md", output / "CHANGELOG.md")

    artifact_records = []
    for path in sorted(staged + [output / "marker-report.json", output / "memory-layout.json", output / "public-key.hex", output / "embedded-public-key.hex", output / "sbom.spdx.json", output / "THIRD_PARTY_NOTICES.md", output / "CHANGELOG.md"]):
        artifact_records.append({"path": path.relative_to(output).as_posix(), "size": path.stat().st_size, "sha256": sha256(path)})
    provenance = {
        "schema_version": 1,
        "model": "SLSA-inspired local provenance; not a SLSA attestation",
        "source_commit": commit,
        "working_tree_dirty": dirty,
        "builder": {"host": "local", "python": sys.version.split()[0]},
        "commands": [
            "make -C firmware/exp045_bootloader_v2 clean all report LAYOUT_PROFILE=stm32f429_1m",
            "make -C firmware/exp065_signed_app clean all signed LAYOUT_PROFILE=stm32f429_1m",
            "make -C firmware/exp066_research_platform_core slot-releases LAYOUT_PROFILE=stm32f429_1m",
            "python3 tools/rdp2_marker.py inspect",
        ],
        "key_mode": key_mode,
        "signing_public_key_fingerprint_sha256": hashlib.sha256(public_key).hexdigest(),
        "embedded_public_key_fingerprint_sha256": hashlib.sha256(embedded_key).hexdigest(),
        "artifacts": artifact_records,
        "tests": {"hardware": "not run by release-candidate tooling", "option_bytes_changed": False},
    }
    write_json(output / "release-provenance.json", provenance)
    manifest = {
        "schema_version": 1,
        "release_version": args.release_version,
        "git": {"commit": commit, "dirty": dirty},
        "target": {"mcu": "STM32F429IGT6", "layout_profile": LAYOUT_PROFILE, "flash_bytes": 1024 * 1024, "sram_bytes": 256 * 1024},
        "versions": {"bootloader": "exp045", "firmware": IMAGE_VERSION, "update_package_format": 2, "rollback_floor": 2},
        "key": {
            "mode": key_mode,
            "signing_public_key_fingerprint_sha256": hashlib.sha256(public_key).hexdigest(),
            "embedded_public_key_fingerprint_sha256": hashlib.sha256(embedded_key).hexdigest(),
            "hardware_installable": public_key == embedded_key,
        },
        "security_status": {"rdp2": "not enabled", "wrp": "not enabled", "option_bytes_changed": False, "hardware_validation": "not run by release-candidate tooling"},
        "artifacts": artifact_records,
        "sbom": {"path": "sbom.spdx.json", "sha256": sha256(output / "sbom.spdx.json")},
        "provenance": {"path": "release-provenance.json", "sha256": sha256(output / "release-provenance.json")},
    }
    write_json(output / "release-manifest.json", manifest)
    checksum_paths = sorted(path for path in output.rglob("*") if path.is_file() and path.name != "SHA256SUMS")
    (output / "SHA256SUMS").write_text("".join(f"{sha256(path)}  {path.relative_to(output).as_posix()}\n" for path in checksum_paths), encoding="ascii")
    archive = output.parent / f"{args.release_version}.tar.gz"
    deterministic_archive(output, archive)
    archive.with_suffix(archive.suffix + ".sha256").write_text(f"{sha256(archive)}  {archive.name}\n", encoding="ascii")
    print(json.dumps({"result": "ok", "release_dir": str(output), "archive": str(archive), "key_mode": key_mode}, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--release-version", default="v1.1.0-rc1-local")
    parser.add_argument("--test-key", action="store_true")
    parser.add_argument("--signing-seed", type=Path)
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    try:
        create_candidate(args)
    except (CandidateError, OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(json.dumps({"result": "failed", "error": str(exc)}, sort_keys=True), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
