#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import release_artifacts  # noqa: E402
from stm32f429_layout import LAYOUT  # noqa: E402

SCHEMA_VERSION = 1
TOOL_VERSION = "memory-integrity-v1"
DEFAULT_BLOCK_SIZE = 4096
MAX_DIFF_EXAMPLES = 8
PUBLIC_TEST_SEED_HEX = "73746d3332663432392d63616e6172792d7631"
TELEMETRY_MAGIC = b"STMR"
TELEMETRY_FIELD_NAMES = (
    "format_version",
    "boot_counter",
    "reset_cause",
    "selected_slot",
    "confirmed_slot",
    "candidate_slot",
    "metadata_state",
    "metadata_sequence",
    "remaining_trial_attempts",
    "image_version",
    "vtor",
    "msp",
    "psp",
    "control",
    "primask",
    "basepri",
    "faultmask",
    "integrity_scan_status",
    "first_changed_region",
    "last_boot_policy_result",
    "last_update_result",
    "trusted_state_flags",
    "untrusted_observation_flags",
    "commit_marker",
    "report_crc",
)
TELEMETRY_STRUCT = struct.Struct("<4s" + ("I" * len(TELEMETRY_FIELD_NAMES)))


class IntegrityError(ValueError):
    pass


@dataclass(frozen=True)
class Region:
    name: str
    start: int
    end: int
    purpose: str
    pattern: str | None = None
    seed: str | None = None

    @property
    def size(self) -> int:
        return self.end - self.start


def checked_add(left: int, right: int, label: str) -> int:
    if left < 0 or right < 0 or left > 0xFFFFFFFF or right > 0xFFFFFFFF:
        raise IntegrityError(f"{label} operands must fit uint32")
    if right > 0xFFFFFFFF - left:
        raise IntegrityError(f"{label} overflows uint32")
    return left + right


def read_bytes(path: Path, label: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise IntegrityError(f"failed to read {label} '{path}': {exc}") from exc


def write_json(path: Path | None, data: dict[str, Any]) -> None:
    text = json.dumps(data, indent=2, sort_keys=True) + "\n"
    if path is None:
        sys.stdout.write(text)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="ascii")


def git_commit() -> str | None:
    try:
        return subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
    except subprocess.CalledProcessError:
        return None


def layout_digest() -> str:
    canonical = json.dumps(LAYOUT, sort_keys=True, separators=(",", ":")).encode("ascii")
    return hashlib.sha256(canonical).hexdigest()


def flash_offset(address: int) -> int:
    if not (LAYOUT["flash_base"] <= address <= LAYOUT["flash_end"]):
        raise IntegrityError(f"address outside flash: 0x{address:08X}")
    return address - LAYOUT["flash_base"]


def load_flash_dump(path: Path) -> bytes:
    data = read_bytes(path, "flash dump")
    expected = LAYOUT["flash_total_size"]
    if len(data) < expected:
        raise IntegrityError(f"truncated dump: {len(data)} bytes < {expected} bytes")
    if len(data) > expected:
        raise IntegrityError(f"oversized dump: {len(data)} bytes > {expected} bytes")
    return data


def default_regions() -> list[Region]:
    header_size = LAYOUT["signed_manifest_size"] + LAYOUT["signed_signature_size"]
    return [
        Region("stage0", LAYOUT["bootloader_base"], LAYOUT["bootloader_end"], "stage0"),
        Region("metadata_copy_0", LAYOUT["boot_metadata_a_base"], LAYOUT["boot_metadata_a_end"], "metadata"),
        Region("metadata_copy_1", LAYOUT["boot_metadata_b_base"], LAYOUT["boot_metadata_b_end"], "metadata"),
        Region("update_metadata", LAYOUT["update_metadata_base"], LAYOUT["update_metadata_end"], "update_metadata"),
        Region(
            "slot_a_manifest_signature",
            LAYOUT["slot_a_manifest_base"],
            checked_add(LAYOUT["slot_a_manifest_base"], header_size, "slot A header"),
            "slot_manifest_signature",
        ),
        Region("slot_a_payload", LAYOUT["slot_a_payload_base"], LAYOUT["slot_a_end"], "slot_payload"),
        Region(
            "slot_b_manifest_signature",
            LAYOUT["slot_b_manifest_base"],
            checked_add(LAYOUT["slot_b_manifest_base"], header_size, "slot B header"),
            "slot_manifest_signature",
        ),
        Region("slot_b_payload", LAYOUT["slot_b_payload_base"], LAYOUT["slot_b_end"], "slot_payload"),
        Region("recovery", LAYOUT["recovery_base"], LAYOUT["recovery_end"], "recovery"),
    ]


def parse_region_spec(spec: str, *, pattern_allowed: bool) -> Region:
    parts = spec.split(":")
    if len(parts) not in (3, 4, 5):
        raise IntegrityError("region must be name:start:end[:pattern[:seed]]")
    name = parts[0]
    if not name or any(ch.isspace() for ch in name):
        raise IntegrityError("region name must be nonempty and contain no whitespace")
    start = int(parts[1], 0)
    end = int(parts[2], 0)
    checked_add(start, end - start, "region")
    if start >= end:
        raise IntegrityError("region start must be below end")
    pattern = parts[3] if len(parts) >= 4 else None
    seed = parts[4] if len(parts) == 5 else None
    if pattern is not None and not pattern_allowed:
        raise IntegrityError("patterns are not accepted for this region argument")
    if pattern is not None:
        require_pattern(pattern)
    return Region(name, start, end, "laboratory_canary", pattern, seed)


def parse_snapshot_specs(specs: list[str], label: str) -> dict[str, int]:
    snapshots: dict[str, int] = {}
    for spec in specs:
        if "=" not in spec:
            raise IntegrityError(f"{label} must use NAME=VALUE")
        name, raw_value = spec.split("=", 1)
        if not name or any(ch.isspace() for ch in name):
            raise IntegrityError(f"{label} name must be nonempty and contain no whitespace")
        if name in snapshots:
            raise IntegrityError(f"duplicate {label}: {name}")
        value = int(raw_value, 0)
        if not (0 <= value <= 0xFFFFFFFF):
            raise IntegrityError(f"{label} value must fit uint32: {name}")
        snapshots[name] = value
    return snapshots


def ranges_overlap(left: Region, right: Region) -> bool:
    return left.start < right.end and right.start < left.end


def validate_regions(regions: list[Region]) -> None:
    seen: set[str] = set()
    for region in regions:
        if region.name in seen:
            raise IntegrityError(f"duplicate region name: {region.name}")
        seen.add(region.name)
        if region.start >= region.end:
            raise IntegrityError(f"invalid region range: {region.name}")
        if region.start < LAYOUT["flash_base"] or region.end > LAYOUT["flash_end"]:
            raise IntegrityError(f"region outside flash: {region.name}")

    for index, left in enumerate(regions):
        for right in regions[index + 1 :]:
            if ranges_overlap(left, right):
                raise IntegrityError(f"overlapping regions: {left.name} and {right.name}")


def validate_snapshot_map(snapshot: object, label: str) -> None:
    if not isinstance(snapshot, dict):
        raise IntegrityError(f"{label} must be an object")
    for name, value in snapshot.items():
        if not isinstance(name, str) or not name or any(ch.isspace() for ch in name):
            raise IntegrityError(f"malformed {label} name")
        if not isinstance(value, int) or not (0 <= value <= 0xFFFFFFFF):
            raise IntegrityError(f"malformed {label} value")


def region_slice(dump: bytes, region: Region) -> bytes:
    start = flash_offset(region.start)
    end = flash_offset(region.end)
    return dump[start:end]


def sectors_for_region(region: Region, dump: bytes) -> list[dict[str, Any]]:
    sectors: list[dict[str, Any]] = []
    for sector in LAYOUT["flash_sectors"]:
        start = max(region.start, sector["base"])
        end = min(region.end, sector["end"])
        if start >= end:
            continue
        chunk = dump[flash_offset(start) : flash_offset(end)]
        sectors.append(
            {
                "id": sector["id"],
                "start": start,
                "end": end,
                "size": end - start,
                "sha512": hashlib.sha512(chunk).hexdigest(),
                "crc32": zlib.crc32(chunk) & 0xFFFFFFFF,
            }
        )
    return sectors


def block_hashes(region: Region, data: bytes, block_size: int) -> list[dict[str, Any]]:
    blocks: list[dict[str, Any]] = []
    for offset in range(0, len(data), block_size):
        chunk = data[offset : offset + block_size]
        blocks.append(
            {
                "index": offset // block_size,
                "start": region.start + offset,
                "end": region.start + offset + len(chunk),
                "size": len(chunk),
                "sha512": hashlib.sha512(chunk).hexdigest(),
            }
        )
    return blocks


def decode_signed_image_manifest(region: Region, dump: bytes) -> dict[str, Any] | None:
    if region.name not in ("slot_a_manifest_signature", "slot_b_manifest_signature"):
        return None
    raw = region_slice(dump, region)
    manifest_size = LAYOUT["signed_manifest_size"]
    if len(raw) < manifest_size or all(byte == 0xFF for byte in raw[:manifest_size]):
        return {"present": False}
    try:
        decoded = release_artifacts.decode_manifest(raw[:manifest_size])
    except Exception as exc:
        return {"present": True, "valid": False, "error": str(exc)}
    return {
        "present": True,
        "valid": True,
        "manifest": decoded,
    }


def region_record(region: Region, dump: bytes, block_size: int) -> dict[str, Any]:
    data = region_slice(dump, region)
    record: dict[str, Any] = {
        "name": region.name,
        "purpose": region.purpose,
        "start": region.start,
        "end": region.end,
        "size": region.size,
        "sha512": hashlib.sha512(data).hexdigest(),
        "crc32": zlib.crc32(data) & 0xFFFFFFFF,
        "sectors": sectors_for_region(region, dump),
        "blocks": block_hashes(region, data, block_size),
    }
    if region.pattern is not None:
        record["expected_pattern"] = {
            "type": region.pattern,
            "seed": region.seed or PUBLIC_TEST_SEED_HEX,
        }
    signed = decode_signed_image_manifest(region, dump)
    if signed is not None:
        record["signed_image"] = signed
    return record


def create_reference(args: argparse.Namespace) -> int:
    try:
        dump = load_flash_dump(args.flash_dump)
        if args.flash_base != LAYOUT["flash_base"]:
            raise IntegrityError("wrong flash base")
        if args.target != LAYOUT["target"]:
            raise IntegrityError("wrong target")
        regions = default_regions()
        for spec in args.canary_region:
            regions.append(parse_region_spec(spec, pattern_allowed=True))
        validate_regions(regions)
        option_snapshot = parse_snapshot_specs(
            args.option_snapshot,
            "option-byte snapshot",
        )
        boot_register_snapshot = parse_snapshot_specs(
            args.boot_register_snapshot,
            "boot-register snapshot",
        )
        block_size = require_block_size(args.block_size)
        manifest = {
            "schema_version": SCHEMA_VERSION,
            "tool_version": TOOL_VERSION,
            "mcu": LAYOUT["mcu"],
            "target": args.target,
            "layout_profile": LAYOUT["profile"],
            "git_commit": args.git_commit or git_commit(),
            "build_identity": args.build_identity,
            "expected_slot": args.expected_slot,
            "flash_base": LAYOUT["flash_base"],
            "flash_end": LAYOUT["flash_end"],
            "flash_size": LAYOUT["flash_total_size"],
            "memory_layout_digest": layout_digest(),
            "slot_layout": {
                "a": {
                    "signed_image_base": LAYOUT["slot_a_signed_image_base"],
                    "payload_base": LAYOUT["slot_a_payload_base"],
                    "end": LAYOUT["slot_a_end"],
                },
                "b": {
                    "signed_image_base": LAYOUT["slot_b_signed_image_base"],
                    "payload_base": LAYOUT["slot_b_payload_base"],
                    "end": LAYOUT["slot_b_end"],
                },
            },
            "block_size": block_size,
            "reference_flash_sha512": hashlib.sha512(dump).hexdigest(),
            "option_byte_snapshot": option_snapshot,
            "boot_state_register_snapshot": boot_register_snapshot,
            "generation": {
                "deterministic": True,
                "endianness": "little",
            },
            "regions": [region_record(region, dump, block_size) for region in regions],
        }
        write_json(args.json_output, manifest)
        if args.report:
            print_human_reference(manifest)
        return 0
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return 1


def require_block_size(value: int) -> int:
    if value <= 0 or value > 65536 or (value & (value - 1)) != 0:
        raise IntegrityError("block size must be a power of two between 1 and 65536")
    return value


def failure_report(exc: Exception) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "result": "failed",
        "error": str(exc),
    }


def load_reference(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="ascii"))
    except json.JSONDecodeError as exc:
        raise IntegrityError(f"malformed JSON: {exc}") from exc
    except OSError as exc:
        raise IntegrityError(f"failed to read reference manifest '{path}': {exc}") from exc
    validate_reference(data)
    return data


def validate_reference(data: dict[str, Any]) -> None:
    if data.get("schema_version") != SCHEMA_VERSION:
        raise IntegrityError("unsupported schema")
    if data.get("target") != LAYOUT["target"]:
        raise IntegrityError("wrong target")
    if data.get("mcu") != LAYOUT["mcu"]:
        raise IntegrityError("wrong MCU")
    if data.get("layout_profile") != LAYOUT["profile"]:
        raise IntegrityError("wrong layout profile")
    if data.get("memory_layout_digest") != layout_digest():
        raise IntegrityError("wrong memory-layout digest")
    validate_snapshot_map(data.get("option_byte_snapshot", {}), "option-byte snapshot")
    validate_snapshot_map(
        data.get("boot_state_register_snapshot", {}),
        "boot-register snapshot",
    )
    regions_raw = data.get("regions")
    if not isinstance(regions_raw, list):
        raise IntegrityError("reference regions must be a list")
    regions: list[Region] = []
    for raw in regions_raw:
        try:
            pattern = None
            seed = None
            if "expected_pattern" in raw:
                pattern = raw["expected_pattern"]["type"]
                seed = raw["expected_pattern"].get("seed")
                require_pattern(pattern)
            regions.append(
                Region(
                    str(raw["name"]),
                    int(raw["start"]),
                    int(raw["end"]),
                    str(raw.get("purpose", "unknown")),
                    pattern,
                    seed,
                )
            )
        except (KeyError, TypeError, ValueError) as exc:
            raise IntegrityError("malformed reference region") from exc
    validate_regions(regions)


def reference_regions(reference: dict[str, Any]) -> list[Region]:
    regions: list[Region] = []
    for raw in reference["regions"]:
        pattern = None
        seed = None
        if "expected_pattern" in raw:
            pattern = raw["expected_pattern"]["type"]
            seed = raw["expected_pattern"].get("seed")
        regions.append(
            Region(
                raw["name"],
                raw["start"],
                raw["end"],
                raw.get("purpose", "unknown"),
                pattern,
                seed,
            )
        )
    return regions


def reference_block_index(reference: dict[str, Any]) -> dict[str, list[tuple[str, int]]]:
    index: dict[str, list[tuple[str, int]]] = {}
    for region in reference["regions"]:
        for block in region.get("blocks", []):
            index.setdefault(block["sha512"], []).append((region["name"], block["index"]))
    return index


def compare_snapshot_map(
    label: str,
    reference: dict[str, int],
    observed: dict[str, int],
) -> list[dict[str, Any]]:
    changes: list[dict[str, Any]] = []
    for name, expected in reference.items():
        if name not in observed:
            changes.append({
                "kind": label,
                "name": name,
                "status": "missing_observation",
                "expected": expected,
            })
            continue
        actual = observed[name]
        if actual != expected:
            changes.append({
                "kind": label,
                "name": name,
                "status": "modified",
                "expected": expected,
                "observed": actual,
                "xor_delta": expected ^ actual,
            })
    for name, actual in observed.items():
        if name not in reference:
            changes.append({
                "kind": label,
                "name": name,
                "status": "unexpected_observation",
                "observed": actual,
            })
    return changes


def count_bits(value: int) -> int:
    return int(value).bit_count()


def expected_pattern_bytes(region: Region) -> bytes | None:
    if region.pattern is None:
        return None
    return bytes(
        pattern_byte(region.pattern, address, region.start, region.seed)
        for address in range(region.start, region.end)
    )


def diff_metrics(
    region: Region,
    expected: bytes | None,
    observed: bytes,
    *,
    max_examples: int,
) -> dict[str, Any]:
    byte_diff = 0
    bit_diff = 0
    one_to_zero = 0
    zero_to_one = 0
    first: int | None = None
    last: int | None = None
    examples: list[dict[str, int]] = []
    if expected is None:
        return {
            "byte_difference_count": None,
            "bit_difference_count": None,
            "one_to_zero_count": None,
            "zero_to_one_count": None,
            "first_differing_address": None,
            "last_differing_address": None,
            "examples": [],
        }
    if len(expected) != len(observed):
        raise IntegrityError("internal diff length mismatch")
    for offset, (exp, obs) in enumerate(zip(expected, observed, strict=True)):
        if exp == obs:
            continue
        address = region.start + offset
        byte_diff += 1
        changed = exp ^ obs
        bit_diff += count_bits(changed)
        one_to_zero += count_bits(exp & ~obs)
        zero_to_one += count_bits((~exp & 0xFF) & obs)
        if first is None:
            first = address
        last = address
        if len(examples) < max_examples:
            examples.append({"address": address, "expected": exp, "observed": obs})
    return {
        "byte_difference_count": byte_diff,
        "bit_difference_count": bit_diff,
        "one_to_zero_count": one_to_zero,
        "zero_to_one_count": zero_to_one,
        "first_differing_address": first,
        "last_differing_address": last,
        "examples": examples,
    }


def classify_region(
    region: dict[str, Any],
    observed: bytes,
    observed_blocks: list[dict[str, Any]],
    block_index: dict[str, list[tuple[str, int]]],
) -> list[dict[str, Any]]:
    heuristics: list[dict[str, Any]] = []
    if observed and all(byte == 0xFF for byte in observed):
        heuristics.append({"type": "erased", "confidence": "high"})
    elif observed:
        erased_count = sum(1 for byte in observed if byte == 0xFF)
        if erased_count * 100 >= len(observed) * 80:
            heuristics.append({"type": "partial_erase_looking", "confidence": "medium"})

    for block in observed_blocks:
        matches = [
            match
            for match in block_index.get(block["sha512"], [])
            if match != (region["name"], block["index"])
        ]
        if matches:
            heuristics.append(
                {
                    "type": "copied_block",
                    "confidence": "medium",
                    "observed_block": block["index"],
                    "matches": [
                        {"region": name, "block": index}
                        for name, index in matches[:MAX_DIFF_EXAMPLES]
                    ],
                }
            )
            break

    reference_blocks = {block["index"]: block["sha512"] for block in region.get("blocks", [])}
    for block in observed_blocks:
        before = reference_blocks.get(block["index"] - 1)
        after = reference_blocks.get(block["index"] + 1)
        if block["sha512"] in (before, after):
            heuristics.append(
                {
                    "type": "shifted_block",
                    "confidence": "low",
                    "observed_block": block["index"],
                }
            )
            break

    return heuristics


def compare_reference(args: argparse.Namespace) -> int:
    try:
        reference = load_reference(args.reference)
        if args.flash_base != reference["flash_base"]:
            raise IntegrityError("wrong flash base")
        observed_dump = load_flash_dump(args.flash_dump)
        reference_dump = load_flash_dump(args.reference_dump) if args.reference_dump else None
        block_size = require_block_size(int(reference["block_size"]))
        block_index = reference_block_index(reference)
        changed_regions = []
        snapshot_changes = compare_snapshot_map(
            "option_byte_snapshot",
            reference.get("option_byte_snapshot", {}),
            parse_snapshot_specs(args.option_snapshot, "option-byte snapshot"),
        )
        snapshot_changes.extend(
            compare_snapshot_map(
                "boot_state_register_snapshot",
                reference.get("boot_state_register_snapshot", {}),
                parse_snapshot_specs(args.boot_register_snapshot, "boot-register snapshot"),
            )
        )

        by_name = {region["name"]: region for region in reference["regions"]}
        for region_def in reference_regions(reference):
            expected = expected_pattern_bytes(region_def)
            if expected is None and reference_dump is not None:
                expected = region_slice(reference_dump, region_def)
            observed = region_slice(observed_dump, region_def)
            observed_sha512 = hashlib.sha512(observed).hexdigest()
            reference_region = by_name[region_def.name]
            observed_blocks = block_hashes(region_def, observed, block_size)
            changed_blocks = [
                block["index"]
                for block, ref_block in zip(
                    observed_blocks,
                    reference_region.get("blocks", []),
                    strict=True,
                )
                if block["sha512"] != ref_block["sha512"]
            ]
            changed = observed_sha512 != reference_region["sha512"]
            entry = {
                "name": region_def.name,
                "status": "modified" if changed else "unchanged",
                "start": region_def.start,
                "end": region_def.end,
                "size": region_def.size,
                "reference_sha512": reference_region["sha512"],
                "observed_sha512": observed_sha512,
                "changed_blocks": changed_blocks,
                "affected_sectors": affected_sectors(region_def, changed_blocks, block_size),
                "differences": diff_metrics(
                    region_def,
                    expected,
                    observed,
                    max_examples=args.max_examples,
                ),
                "heuristics": classify_region(
                    reference_region,
                    observed,
                    observed_blocks,
                    block_index,
                )
                if changed
                else [],
            }
            if changed:
                changed_regions.append(entry)

        report = {
            "schema_version": SCHEMA_VERSION,
            "result": "changed" if changed_regions or snapshot_changes else "ok",
            "target": reference["target"],
            "memory_layout_digest": reference["memory_layout_digest"],
            "changed_region_count": len(changed_regions),
            "snapshot_change_count": len(snapshot_changes),
            "changed_regions": changed_regions,
            "snapshot_changes": snapshot_changes,
        }
        write_json(args.json_output, report)
        if args.report:
            print_human_compare(report)
        return 1 if changed_regions or snapshot_changes else 0
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return 2


def affected_sectors(region: Region, changed_blocks: list[int], block_size: int) -> list[int]:
    affected: set[int] = set()
    for block_index in changed_blocks:
        start = region.start + (block_index * block_size)
        end = min(region.end, start + block_size)
        for sector in LAYOUT["flash_sectors"]:
            if start < sector["end"] and sector["base"] < end:
                affected.add(sector["id"])
    return sorted(affected)


def require_pattern(pattern: str) -> None:
    if pattern not in {
        "all00",
        "allff",
        "aa55",
        "address",
        "prng",
        "walking_one",
        "walking_zero",
    }:
        raise IntegrityError(f"unsupported canary pattern: {pattern}")


def seed_bytes(seed: str | None) -> bytes:
    value = seed or PUBLIC_TEST_SEED_HEX
    try:
        data = bytes.fromhex(value)
    except ValueError as exc:
        raise IntegrityError("canary seed must be hexadecimal") from exc
    if len(data) == 0:
        raise IntegrityError("canary seed must be nonempty")
    return data


def pattern_byte(pattern: str, address: int, region_start: int, seed: str | None) -> int:
    require_pattern(pattern)
    if pattern == "all00":
        return 0x00
    if pattern == "allff":
        return 0xFF
    if pattern == "aa55":
        return 0xAA if ((address - region_start) & 1) == 0 else 0x55
    if pattern == "address":
        return address & 0xFF
    if pattern == "walking_one":
        return 1 << ((address - region_start) & 7)
    if pattern == "walking_zero":
        return (~(1 << ((address - region_start) & 7))) & 0xFF
    digest = hashlib.sha256(seed_bytes(seed) + struct.pack("<I", address)).digest()
    return digest[0]


def protected_regions(active_slot: str | None) -> list[Region]:
    regions = [
        Region("stage0", LAYOUT["bootloader_base"], LAYOUT["bootloader_end"], "stage0"),
        Region("metadata_copy_0", LAYOUT["boot_metadata_a_base"], LAYOUT["boot_metadata_a_end"], "metadata"),
        Region("metadata_copy_1", LAYOUT["boot_metadata_b_base"], LAYOUT["boot_metadata_b_end"], "metadata"),
        Region("update_metadata", LAYOUT["update_metadata_base"], LAYOUT["update_metadata_end"], "update_metadata"),
        Region("recovery", LAYOUT["recovery_base"], LAYOUT["recovery_end"], "recovery"),
    ]
    if active_slot in ("a", "b"):
        slot = LAYOUT[f"slot_{active_slot}"]
        regions.append(
            Region(
                f"active_slot_{active_slot}",
                slot["signed_image_base"],
                slot["end"],
                "active_slot",
            )
        )
    return regions


def parse_lab_region(args: argparse.Namespace) -> Region:
    region = parse_region_spec(args.region, pattern_allowed=True)
    if region.start < LAYOUT["flash_base"] or region.end > LAYOUT["flash_end"]:
        raise IntegrityError("canary region must be inside internal flash")
    allowlist = [
        parse_region_spec(spec, pattern_allowed=False)
        for spec in args.allow_lab_region
    ]
    if not allowlist:
        raise IntegrityError("no explicit laboratory canary region was allowed")
    if not any(region.start >= allowed.start and region.end <= allowed.end for allowed in allowlist):
        raise IntegrityError("canary region is not inside an explicit laboratory allowlist")
    for protected in protected_regions(args.active_slot):
        if ranges_overlap(region, protected):
            raise IntegrityError(f"canary region overlaps protected {protected.name}")
    return region


def canary_provision(args: argparse.Namespace) -> int:
    try:
        region = parse_lab_region(args)
        data = expected_pattern_bytes(region)
        if data is None:
            raise IntegrityError("canary pattern missing")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        report = {
            "schema_version": SCHEMA_VERSION,
            "result": "ok",
            "region": {
                "name": region.name,
                "start": region.start,
                "end": region.end,
                "size": region.size,
                "pattern": region.pattern,
                "seed": region.seed or PUBLIC_TEST_SEED_HEX,
                "sha512": hashlib.sha512(data).hexdigest(),
            },
        }
        write_json(args.json_output, report)
        return 0
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return 1


def canary_validate(args: argparse.Namespace) -> int:
    try:
        dump = load_flash_dump(args.flash_dump)
        region = parse_region_spec(args.region, pattern_allowed=True)
        expected = expected_pattern_bytes(region)
        if expected is None:
            raise IntegrityError("canary pattern missing")
        observed = region_slice(dump, region)
        differences = diff_metrics(region, expected, observed, max_examples=args.max_examples)
        changed = differences["byte_difference_count"] != 0
        report = {
            "schema_version": SCHEMA_VERSION,
            "result": "changed" if changed else "ok",
            "region": region.name,
            "differences": differences,
        }
        write_json(args.json_output, report)
        return 1 if changed else 0
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return 2


def decode_telemetry_bytes(data: bytes) -> dict[str, Any]:
    if len(data) != TELEMETRY_STRUCT.size:
        raise IntegrityError("malformed telemetry length")
    unpacked = TELEMETRY_STRUCT.unpack(data)
    if unpacked[0] != TELEMETRY_MAGIC:
        raise IntegrityError("malformed telemetry magic")
    values = dict(zip(TELEMETRY_FIELD_NAMES, unpacked[1:], strict=True))
    expected_crc = zlib.crc32(data[:-4]) & 0xFFFFFFFF
    if values["report_crc"] != expected_crc:
        raise IntegrityError("bad report CRC")
    trusted = {
        "selected_slot": values["selected_slot"],
        "confirmed_slot": values["confirmed_slot"],
        "candidate_slot": values["candidate_slot"],
        "metadata_state": values["metadata_state"],
        "metadata_sequence": values["metadata_sequence"],
        "remaining_trial_attempts": values["remaining_trial_attempts"],
        "image_version": values["image_version"],
        "last_boot_policy_result": values["last_boot_policy_result"],
        "last_update_result": values["last_update_result"],
    }
    observations = {
        key: values[key]
        for key in (
            "boot_counter",
            "reset_cause",
            "vtor",
            "msp",
            "psp",
            "control",
            "primask",
            "basepri",
            "faultmask",
            "integrity_scan_status",
            "first_changed_region",
        )
    }
    return {
        "schema_version": SCHEMA_VERSION,
        "result": "ok",
        "telemetry": values,
        "trusted_security_state": trusted,
        "untrusted_diagnostic_observations": observations,
    }


def decode_telemetry(args: argparse.Namespace) -> int:
    try:
        report = decode_telemetry_bytes(read_bytes(args.input, "telemetry"))
        write_json(args.json_output, report)
        return 0
    except Exception as exc:
        write_json(args.json_output, failure_report(exc))
        return 1


def print_human_reference(manifest: dict[str, Any]) -> None:
    print(f"target {manifest['target']}")
    print(f"layout {manifest['memory_layout_digest']}")
    for name, value in manifest.get("option_byte_snapshot", {}).items():
        print(f"option {name}=0x{value:08X}")
    for name, value in manifest.get("boot_state_register_snapshot", {}).items():
        print(f"boot-register {name}=0x{value:08X}")
    for region in manifest["regions"]:
        print(
            f"{region['name']} 0x{region['start']:08X}-0x{region['end']:08X} "
            f"{region['size']} {region['sha512'][:16]}"
        )


def print_human_compare(report: dict[str, Any]) -> None:
    print(f"result {report['result']}")
    for region in report["changed_regions"]:
        print(f"modified {region['name']} blocks={region['changed_blocks']}")
        diff = region["differences"]
        if diff["first_differing_address"] is not None:
            print(
                " first=0x%08X last=0x%08X bytes=%s bits=%s"
                % (
                    diff["first_differing_address"],
                    diff["last_differing_address"],
                    diff["byte_difference_count"],
                    diff["bit_difference_count"],
                )
            )
        for heuristic in region["heuristics"]:
            print(f" heuristic {heuristic['type']} confidence={heuristic['confidence']}")
    for change in report.get("snapshot_changes", []):
        print(
            f"snapshot {change['kind']} {change['name']} "
            f"{change['status']}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="STM32F429 memory integrity tooling")
    sub = parser.add_subparsers(dest="command", required=True)

    create = sub.add_parser("create-reference")
    create.add_argument("--flash-dump", required=True, type=Path)
    create.add_argument("--json-output", type=Path)
    create.add_argument("--report", action="store_true")
    create.add_argument("--target", default=LAYOUT["target"])
    create.add_argument("--flash-base", type=lambda value: int(value, 0), default=LAYOUT["flash_base"])
    create.add_argument("--git-commit")
    create.add_argument("--build-identity", default="unspecified")
    create.add_argument("--expected-slot", choices=("a", "b"), default="a")
    create.add_argument("--block-size", type=int, default=DEFAULT_BLOCK_SIZE)
    create.add_argument("--canary-region", action="append", default=[])
    create.add_argument("--option-snapshot", action="append", default=[])
    create.add_argument("--boot-register-snapshot", action="append", default=[])
    create.set_defaults(func=create_reference)

    compare = sub.add_parser("compare")
    compare.add_argument("--reference", required=True, type=Path)
    compare.add_argument("--flash-dump", required=True, type=Path)
    compare.add_argument("--reference-dump", type=Path)
    compare.add_argument("--flash-base", type=lambda value: int(value, 0), default=LAYOUT["flash_base"])
    compare.add_argument("--json-output", type=Path)
    compare.add_argument("--report", action="store_true")
    compare.add_argument("--max-examples", type=int, default=MAX_DIFF_EXAMPLES)
    compare.add_argument("--option-snapshot", action="append", default=[])
    compare.add_argument("--boot-register-snapshot", action="append", default=[])
    compare.set_defaults(func=compare_reference)

    provision = sub.add_parser("canary-provision")
    provision.add_argument("--region", required=True)
    provision.add_argument("--allow-lab-region", action="append", default=[])
    provision.add_argument("--active-slot", choices=("a", "b"))
    provision.add_argument("--output", required=True, type=Path)
    provision.add_argument("--json-output", type=Path)
    provision.set_defaults(func=canary_provision)

    validate = sub.add_parser("canary-validate")
    validate.add_argument("--flash-dump", required=True, type=Path)
    validate.add_argument("--region", required=True)
    validate.add_argument("--json-output", type=Path)
    validate.add_argument("--max-examples", type=int, default=MAX_DIFF_EXAMPLES)
    validate.set_defaults(func=canary_validate)

    telemetry = sub.add_parser("decode-telemetry")
    telemetry.add_argument("--input", required=True, type=Path)
    telemetry.add_argument("--json-output", type=Path)
    telemetry.set_defaults(func=decode_telemetry)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
