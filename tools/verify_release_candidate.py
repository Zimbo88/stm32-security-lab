#!/usr/bin/env python3
"""Verify a local release candidate without hardware or network access."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess  # nosec B404 - verification uses fixed argv and no shell
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class VerificationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="ascii"))
    except (OSError, json.JSONDecodeError) as exc:
        raise VerificationError(f"invalid JSON: {path}") from exc
    if not isinstance(value, dict):
        raise VerificationError(f"JSON object expected: {path}")
    return value


def run(command: list[str]) -> None:
    try:
        subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)  # nosec B603 - shell is disabled
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "stderr", "") or str(exc)
        raise VerificationError(f"verification command failed: {command[0]}: {detail.strip()}") from exc


def verify_checksums(root: Path) -> None:
    sums = root / "SHA256SUMS"
    expected: dict[str, str] = {}
    for line in sums.read_text(encoding="ascii").splitlines():
        digest, relative = line.split("  ", 1)
        expected[relative] = digest
    actual = {relative: sha256(root / relative) for relative in expected if (root / relative).is_file()}
    if actual != expected:
        raise VerificationError("SHA256SUMS mismatch")


def verify_manifest(root: Path, manifest: dict[str, object]) -> None:
    if manifest.get("schema_version") != 1:
        raise VerificationError("unsupported release manifest schema")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list):
        raise VerificationError("release manifest has no artifact list")
    for item in artifacts:
        if not isinstance(item, dict):
            raise VerificationError("malformed artifact record")
        relative = item.get("path")
        if not isinstance(relative, str) or Path(relative).is_absolute() or ".." in Path(relative).parts:
            raise VerificationError("artifact path is not repository-relative")
        path = root / relative
        if not path.is_file() or path.stat().st_size != item.get("size") or sha256(path) != item.get("sha256"):
            raise VerificationError(f"artifact hash/size mismatch: {relative}")
    for section in ("sbom", "provenance"):
        record = manifest.get(section)
        if not isinstance(record, dict):
            raise VerificationError(f"missing manifest section: {section}")
        path = root / str(record.get("path"))
        if not path.is_file() or sha256(path) != record.get("sha256"):
            raise VerificationError(f"{section} hash mismatch")
    key = root / "public-key.hex"
    public_key = bytes.fromhex(key.read_text(encoding="ascii").strip())
    if len(public_key) != 32:
        raise VerificationError("candidate public key is not 32 bytes")
    key_section = manifest.get("key")
    if not isinstance(key_section, dict) or hashlib.sha256(public_key).hexdigest() != key_section.get("signing_public_key_fingerprint_sha256"):
        raise VerificationError("candidate signing-key fingerprint mismatch")
    marker = load_json(root / "marker-report.json")
    if marker.get("all_verified") is not True:
        raise VerificationError("marker report is not fully verified")
    sbom = load_json(root / "sbom.spdx.json")
    if sbom.get("spdxVersion") != "SPDX-2.3":
        raise VerificationError("SBOM is not SPDX-2.3")


def verify_signatures(root: Path) -> None:
    public_key = (root / "public-key.hex").read_text(encoding="ascii").strip()
    run([
        sys.executable, "tools/release_artifacts.py", "verify-signed",
        "--signed-image", str(root / "legacy/signed-image.bin"),
        "--application", str(root / "legacy/legacy-app.bin"),
        "--public-key-hex", public_key,
    ])
    for slot in ("a", "b"):
        run([
            sys.executable, "tools/update_package.py", "verify",
            "--package", str(root / f"slot-{slot}/update-package.bin"),
            "--slot", slot,
            "--application", str(root / f"slot-{slot}/slot-{slot}.bin"),
            "--public-key-hex", public_key,
        ])


def verify_archive(root: Path) -> None:
    archive = root.parent / f"{root.name}.tar.gz"
    sidecar = archive.with_suffix(archive.suffix + ".sha256")
    if not archive.is_file() or not sidecar.is_file():
        raise VerificationError("deterministic archive or sidecar is missing")
    expected, name = sidecar.read_text(encoding="ascii").strip().split("  ", 1)
    if name != archive.name or expected != sha256(archive):
        raise VerificationError("release archive hash mismatch")
    with tarfile.open(archive, "r:gz") as tar:
        names = {member.name for member in tar.getmembers() if member.isfile()}
    expected_names = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
    if names != expected_names:
        raise VerificationError("archive file set differs from release directory")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.release_dir.resolve()
    try:
        if not root.is_dir():
            raise VerificationError(f"release directory does not exist: {root}")
        with tempfile.TemporaryDirectory(prefix="candidate_verify_") as temp:
            run([sys.executable, "tools/publication_scan.py", str(root), "--output", str(Path(temp) / "publication.json")])
        manifest = load_json(root / "release-manifest.json")
        verify_manifest(root, manifest)
        verify_checksums(root)
        verify_signatures(root)
        verify_archive(root)
        result = {"result": "ok", "release_dir": str(root), "release_version": manifest.get("release_version")}
    except (VerificationError, OSError, ValueError) as exc:
        result = {"result": "failed", "error": str(exc)}
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="ascii")
    print(text, end="")
    return 0 if result["result"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
