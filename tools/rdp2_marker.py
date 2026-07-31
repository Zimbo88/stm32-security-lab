#!/usr/bin/env python3
"""Inspect and search synthetic RDP2 research markers.

This tool only reads ELF files, firmware images, and later dump files.  It has
no programmer, option-byte, reset, or target-connection functionality.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SPEC = ROOT / "config" / "rdp2_research_markers.json"


class MarkerError(ValueError):
    """Raised for an invalid marker specification or artifact."""


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_spec(path: Path) -> dict[str, Any]:
    try:
        spec = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise MarkerError(f"cannot read marker specification {path}: {exc}") from exc
    if spec.get("schema_version") != 1 or not isinstance(spec.get("markers"), list):
        raise MarkerError("unsupported or malformed marker specification")
    return spec


def repo_resolve(value: str | None) -> Path | None:
    if value is None:
        return None
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def marker_bytes(marker: dict[str, Any]) -> bytes:
    value = marker.get("value")
    if not isinstance(value, str) or not value:
        raise MarkerError(f"marker {marker.get('id', '<unknown>')} has no value")
    return value.encode("ascii") + b"\0"


def defined_symbols(elf: Path) -> dict[str, int]:
    nm = shutil.which("arm-none-eabi-nm") or shutil.which("nm")
    if nm is None:
        raise MarkerError("neither arm-none-eabi-nm nor nm is available")
    try:
        result = subprocess.run(
            [nm, "-P", "--defined-only", str(elf)],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "stderr", "") or str(exc)
        raise MarkerError(f"failed to read ELF symbols from {elf}: {detail.strip()}") from exc

    symbols: dict[str, int] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 3:
            continue
        name, _symbol_type, value = fields[:3]
        try:
            symbols[name] = int(value, 16)
        except ValueError:
            continue
    return symbols


def marker_record(marker: dict[str, Any]) -> dict[str, Any]:
    data = marker_bytes(marker)
    record: dict[str, Any] = {
        "id": marker["id"],
        "storage": marker["storage"],
        "value_ascii": marker["value"],
        "length": len(data),
        "sha256": sha256_hex(data),
        "symbol_start": marker.get("symbol_start"),
        "symbol_end": marker.get("symbol_end"),
        "image_base": marker.get("image_base"),
    }
    if marker.get("reference_path"):
        record["reference_path"] = marker["reference_path"]
    if marker.get("target_installation"):
        record["target_installation"] = marker["target_installation"]
    return record


def inspect_one(marker: dict[str, Any]) -> dict[str, Any]:
    expected = marker_bytes(marker)
    record = marker_record(marker)
    elf = repo_resolve(marker.get("elf"))
    binary = repo_resolve(marker.get("binary"))

    if elf is not None:
        if not elf.is_file():
            raise MarkerError(f"missing ELF for {marker['id']}: {elf}")
        symbols = defined_symbols(elf)
        start_name = marker.get("symbol_start")
        end_name = marker.get("symbol_end")
        if start_name not in symbols or end_name not in symbols:
            raise MarkerError(f"missing marker symbols for {marker['id']} in {elf}")
        start = symbols[start_name]
        end = symbols[end_name]
        if end < start or end - start != len(expected):
            raise MarkerError(
                f"ELF marker size mismatch for {marker['id']}: {end - start} != {len(expected)}"
            )
        record["elf"] = str(elf.relative_to(ROOT))
        record["address"] = f"0x{start:08X}"
        record["symbol_size"] = end - start

        if binary is not None:
            if not binary.is_file():
                raise MarkerError(f"missing binary for {marker['id']}: {binary}")
            image_base = int(str(marker["image_base"]), 0)
            offset = start - image_base
            if offset < 0:
                raise MarkerError(f"marker address precedes image base for {marker['id']}")
            image = binary.read_bytes()
            actual = image[offset : offset + len(expected)]
            if actual != expected:
                raise MarkerError(f"binary marker bytes do not match for {marker['id']}")
            record["binary"] = str(binary.relative_to(ROOT))
            record["binary_offset"] = f"0x{offset:X}"
            record["binary_match"] = True
    elif marker.get("reference_path"):
        reference = repo_resolve(str(marker["reference_path"]))
        if reference is None or not reference.is_file():
            raise MarkerError(f"missing configuration marker reference: {reference}")
        if marker["value"] not in reference.read_text(encoding="utf-8"):
            raise MarkerError(f"configuration marker value is absent: {reference}")
        record["reference_match"] = True
        record["address"] = None
    else:
        raise MarkerError(f"marker {marker['id']} has neither ELF nor reference storage")

    return record


def inspect_spec(spec: dict[str, Any], *, spec_path: Path = DEFAULT_SPEC) -> dict[str, Any]:
    records = [inspect_one(marker) for marker in spec["markers"]]
    return {
        "schema_version": 1,
        "result": "ok",
        "spec": str(spec_path.resolve().relative_to(ROOT)),
        "markers": records,
        "all_verified": True,
    }


def scan_dump(path: Path, spec: dict[str, Any]) -> dict[str, Any]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise MarkerError(f"cannot read dump {path}: {exc}") from exc

    markers: list[dict[str, Any]] = []
    for marker in spec["markers"]:
        expected = marker_bytes(marker)
        full: list[int] = []
        position = data.find(expected)
        while position >= 0:
            full.append(position)
            position = data.find(expected, position + 1)

        partial: dict[str, list[int]] = {}
        for length in range(len(expected) - 1, 3, -1):
            prefix = expected[:length]
            positions: list[int] = []
            position = data.find(prefix)
            while position >= 0:
                positions.append(position)
                position = data.find(prefix, position + 1)
            if positions:
                partial[str(length)] = positions

        markers.append({
            "id": marker["id"],
            "length": len(expected),
            "sha256": sha256_hex(expected),
            "full_matches": full,
            "partial_prefix_matches": partial,
        })

    return {
        "schema_version": 1,
        "result": "ok",
        "dump": str(path),
        "dump_size": len(data),
        "dump_sha256": sha256_hex(data),
        "markers": markers,
    }


def emit(payload: dict[str, Any], output: Path | None) -> None:
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if output is None:
        sys.stdout.write(text)
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="ascii")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, default=DEFAULT_SPEC)
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect = subparsers.add_parser("inspect", help="verify ELF symbols and reference binaries")
    inspect.add_argument("--output", type=Path)

    scan = subparsers.add_parser("scan", help="search complete and partial markers in a dump")
    scan.add_argument("--dump", required=True, type=Path)
    scan.add_argument("--output", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        spec_path = args.spec.resolve()
        spec = load_spec(spec_path)
        if args.command == "inspect":
            payload = inspect_spec(spec, spec_path=spec_path)
        else:
            payload = scan_dump(args.dump, spec)
        emit(payload, args.output)
        return 0
    except MarkerError as exc:
        emit({"schema_version": 1, "result": "failed", "error": str(exc)}, args.output)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
