from __future__ import annotations

import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "memory_integrity.py"

sys.path.insert(0, str(ROOT / "tools"))
import memory_integrity  # noqa: E402
from stm32f429_layout import LAYOUT  # noqa: E402


def run_tool(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        text=True,
        capture_output=True,
        check=False,
    )


def flash_image(fill: int = 0xFF) -> bytearray:
    image = bytearray([fill] * LAYOUT["flash_total_size"])
    for offset in range(0, LAYOUT["flash_total_size"], 4):
        image[offset : offset + 4] = struct.pack("<I", offset)
    return image


def write_flash(path: Path, data: bytes | bytearray) -> Path:
    path.write_bytes(bytes(data))
    return path


def make_reference(tmp_path: Path, dump: Path) -> Path:
    reference = tmp_path / "reference.json"
    result = run_tool(
        "create-reference",
        "--flash-dump",
        str(dump),
        "--git-commit",
        "0123456789abcdef",
        "--build-identity",
        "test-build",
        "--json-output",
        str(reference),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return reference


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="ascii"))


def test_valid_unchanged_reference(tmp_path: Path) -> None:
    reference_dump = write_flash(tmp_path / "flash.bin", flash_image())
    reference = make_reference(tmp_path, reference_dump)
    report = tmp_path / "compare.json"

    result = run_tool(
        "compare",
        "--reference",
        str(reference),
        "--flash-dump",
        str(reference_dump),
        "--reference-dump",
        str(reference_dump),
        "--json-output",
        str(report),
    )

    assert result.returncode == 0, result.stdout + result.stderr
    data = load(report)
    assert data["result"] == "ok"
    assert data["changed_region_count"] == 0


def test_one_changed_byte_and_bit_transition_counts(tmp_path: Path) -> None:
    original = flash_image()
    modified = bytearray(original)
    address = LAYOUT["slot_a_payload_base"] + 123
    offset = address - LAYOUT["flash_base"]
    modified[offset] ^= 0x03
    reference_dump = write_flash(tmp_path / "reference.bin", original)
    modified_dump = write_flash(tmp_path / "modified.bin", modified)
    reference = make_reference(tmp_path, reference_dump)
    report = tmp_path / "compare.json"

    result = run_tool(
        "compare",
        "--reference",
        str(reference),
        "--flash-dump",
        str(modified_dump),
        "--reference-dump",
        str(reference_dump),
        "--json-output",
        str(report),
    )

    assert result.returncode == 1
    changed = load(report)["changed_regions"][0]
    assert changed["name"] == "slot_a_payload"
    assert changed["differences"]["byte_difference_count"] == 1
    assert changed["differences"]["bit_difference_count"] == 2
    assert changed["differences"]["first_differing_address"] == address
    assert (
        changed["differences"]["one_to_zero_count"] +
        changed["differences"]["zero_to_one_count"]
    ) == 2


def test_multiple_sectors_and_erased_heuristics(tmp_path: Path) -> None:
    original = flash_image()
    modified = bytearray(original)
    for sector_id in (5, 6):
        sector = LAYOUT["flash_sectors"][sector_id]
        start = sector["base"] - LAYOUT["flash_base"]
        end = sector["end"] - LAYOUT["flash_base"]
        modified[start:end] = b"\xFF" * (end - start)
    reference_dump = write_flash(tmp_path / "reference.bin", original)
    modified_dump = write_flash(tmp_path / "modified.bin", modified)
    reference = make_reference(tmp_path, reference_dump)
    report = tmp_path / "compare.json"

    result = run_tool(
        "compare",
        "--reference",
        str(reference),
        "--flash-dump",
        str(modified_dump),
        "--reference-dump",
        str(reference_dump),
        "--json-output",
        str(report),
    )

    assert result.returncode == 1
    changed = load(report)["changed_regions"]
    affected = sorted({sector for region in changed for sector in region["affected_sectors"]})
    assert {5, 6}.issubset(affected)
    assert any(
        item["type"] in {"erased", "partial_erase_looking"}
        for region in changed
        for item in region["heuristics"]
    )


def test_copied_and_shifted_block_heuristics(tmp_path: Path) -> None:
    original = flash_image()
    modified = bytearray(original)
    block = 4096
    stage0_offset = LAYOUT["bootloader_base"] - LAYOUT["flash_base"]
    slot_offset = LAYOUT["slot_a_payload_base"] - LAYOUT["flash_base"]
    modified[slot_offset : slot_offset + block] = original[stage0_offset : stage0_offset + block]
    shifted_offset = slot_offset + block
    modified[shifted_offset : shifted_offset + block] = original[slot_offset : slot_offset + block]
    reference_dump = write_flash(tmp_path / "reference.bin", original)
    modified_dump = write_flash(tmp_path / "modified.bin", modified)
    reference = make_reference(tmp_path, reference_dump)
    report = tmp_path / "compare.json"

    result = run_tool(
        "compare",
        "--reference",
        str(reference),
        "--flash-dump",
        str(modified_dump),
        "--reference-dump",
        str(reference_dump),
        "--json-output",
        str(report),
    )

    assert result.returncode == 1
    heuristics = load(report)["changed_regions"][0]["heuristics"]
    assert any(item["type"] == "copied_block" for item in heuristics)
    assert any(item["type"] == "shifted_block" for item in heuristics)


def test_truncated_oversized_wrong_target_and_layout_digest(tmp_path: Path) -> None:
    truncated = write_flash(tmp_path / "truncated.bin", b"\xFF" * 16)
    oversized = write_flash(tmp_path / "oversized.bin", b"\xFF" * (LAYOUT["flash_total_size"] + 1))
    report = tmp_path / "report.json"
    assert run_tool("create-reference", "--flash-dump", str(truncated), "--json-output", str(report)).returncode == 1
    assert "truncated" in load(report)["error"]
    assert run_tool("create-reference", "--flash-dump", str(oversized), "--json-output", str(report)).returncode == 1
    assert "oversized" in load(report)["error"]

    reference_dump = write_flash(tmp_path / "flash.bin", flash_image())
    assert run_tool(
        "create-reference",
        "--flash-dump",
        str(reference_dump),
        "--target",
        "WRONG",
        "--json-output",
        str(report),
    ).returncode == 1
    assert "wrong target" in load(report)["error"]

    reference = make_reference(tmp_path, reference_dump)
    data = load(reference)
    data["memory_layout_digest"] = "0" * 64
    bad_reference = tmp_path / "bad-layout.json"
    bad_reference.write_text(json.dumps(data), encoding="ascii")
    assert run_tool(
        "compare",
        "--reference",
        str(bad_reference),
        "--flash-dump",
        str(reference_dump),
        "--json-output",
        str(report),
    ).returncode == 2
    assert "wrong memory-layout digest" in load(report)["error"]


def test_malformed_reference_duplicate_overlap_and_address_overflow(tmp_path: Path) -> None:
    dump = write_flash(tmp_path / "flash.bin", flash_image())
    report = tmp_path / "report.json"
    malformed = tmp_path / "malformed.json"
    malformed.write_text("{", encoding="ascii")
    assert run_tool("compare", "--reference", str(malformed), "--flash-dump", str(dump), "--json-output", str(report)).returncode == 2
    assert "malformed JSON" in load(report)["error"]

    reference = make_reference(tmp_path, dump)
    data = load(reference)
    data["regions"][1]["name"] = data["regions"][0]["name"]
    duplicate = tmp_path / "duplicate.json"
    duplicate.write_text(json.dumps(data), encoding="ascii")
    assert run_tool("compare", "--reference", str(duplicate), "--flash-dump", str(dump), "--json-output", str(report)).returncode == 2
    assert "duplicate region" in load(report)["error"]

    assert run_tool(
        "create-reference",
        "--flash-dump",
        str(dump),
        "--canary-region",
        "overflow:0xfffffff0:0x100000010:address",
        "--json-output",
        str(report),
    ).returncode == 1
    assert "overflows" in load(report)["error"]


def test_bounded_difference_output(tmp_path: Path) -> None:
    original = flash_image()
    modified = bytearray(original)
    base = LAYOUT["slot_b_payload_base"] - LAYOUT["flash_base"]
    for index in range(20):
        modified[base + index] ^= 0x01
    reference_dump = write_flash(tmp_path / "reference.bin", original)
    modified_dump = write_flash(tmp_path / "modified.bin", modified)
    reference = make_reference(tmp_path, reference_dump)
    report = tmp_path / "compare.json"

    result = run_tool(
        "compare",
        "--reference",
        str(reference),
        "--flash-dump",
        str(modified_dump),
        "--reference-dump",
        str(reference_dump),
        "--max-examples",
        "3",
        "--json-output",
        str(report),
    )

    assert result.returncode == 1
    examples = load(report)["changed_regions"][0]["differences"]["examples"]
    assert len(examples) == 3


def test_canary_provision_protection_and_determinism(tmp_path: Path) -> None:
    report = tmp_path / "canary.json"
    output_a = tmp_path / "canary-a.bin"
    output_b = tmp_path / "canary-b.bin"
    lab_start = LAYOUT["update_metadata_base"]
    lab_end = lab_start + 64
    spec = f"lab:0x{lab_start:08x}:0x{lab_end:08x}:prng"
    allow = f"lab:0x{lab_start:08x}:0x{lab_end:08x}"

    assert run_tool(
        "canary-provision",
        "--region",
        spec,
        "--allow-lab-region",
        allow,
        "--output",
        str(output_a),
        "--json-output",
        str(report),
    ).returncode == 1
    assert "protected update_metadata" in load(report)["error"]

    lab_start = LAYOUT["slot_b_payload_base"]
    lab_end = lab_start + 64
    spec = f"lab:0x{lab_start:08x}:0x{lab_end:08x}:address"
    allow = f"lab:0x{lab_start:08x}:0x{lab_end:08x}"
    assert run_tool(
        "canary-provision",
        "--region",
        spec,
        "--allow-lab-region",
        allow,
        "--active-slot",
        "a",
        "--output",
        str(output_a),
        "--json-output",
        str(report),
    ).returncode == 0
    assert run_tool(
        "canary-provision",
        "--region",
        spec,
        "--allow-lab-region",
        allow,
        "--active-slot",
        "a",
        "--output",
        str(output_b),
        "--json-output",
        str(report),
    ).returncode == 0
    assert output_a.read_bytes() == output_b.read_bytes()


def test_canary_rejects_protected_and_nonflash_regions(tmp_path: Path) -> None:
    report = tmp_path / "report.json"
    output = tmp_path / "out.bin"
    cases = [
        ("stage0", LAYOUT["bootloader_base"], LAYOUT["bootloader_base"] + 16, None),
        ("metadata", LAYOUT["boot_metadata_a_base"], LAYOUT["boot_metadata_a_base"] + 16, None),
        ("active", LAYOUT["slot_a_payload_base"], LAYOUT["slot_a_payload_base"] + 16, "a"),
        ("recovery", LAYOUT["recovery_base"], LAYOUT["recovery_base"] + 16, None),
        ("option", 0x1FFFC000, 0x1FFFC010, None),
        ("otp", 0x1FFF7800, 0x1FFF7810, None),
    ]
    for name, start, end, active_slot in cases:
        args = [
            "canary-provision",
            "--region",
            f"{name}:0x{start:08x}:0x{end:08x}:aa55",
            "--allow-lab-region",
            f"{name}:0x{start:08x}:0x{end:08x}",
            "--output",
            str(output),
            "--json-output",
            str(report),
        ]
        if active_slot is not None:
            args.extend(["--active-slot", active_slot])
        assert run_tool(*args).returncode == 1


def test_canary_validate_counts_transitions(tmp_path: Path) -> None:
    dump = bytearray(b"\xFF" * LAYOUT["flash_total_size"])
    start = LAYOUT["slot_b_payload_base"]
    end = start + 16
    for address in range(start, end):
        dump[address - LAYOUT["flash_base"]] = memory_integrity.pattern_byte("walking_one", address, start, None)
    dump[start - LAYOUT["flash_base"]] = 0x00
    flash = write_flash(tmp_path / "flash.bin", dump)
    report = tmp_path / "report.json"

    result = run_tool(
        "canary-validate",
        "--flash-dump",
        str(flash),
        "--region",
        f"lab:0x{start:08x}:0x{end:08x}:walking_one",
        "--json-output",
        str(report),
    )

    assert result.returncode == 1
    differences = load(report)["differences"]
    assert differences["byte_difference_count"] == 1
    assert differences["one_to_zero_count"] == 1
    assert differences["zero_to_one_count"] == 0


def telemetry_blob(*, crc_delta: int = 0) -> bytes:
    fields = [1] + list(range(1, len(memory_integrity.TELEMETRY_FIELD_NAMES)))
    fields[-1] = 0
    without_crc = struct.pack(
        "<4s" + ("I" * len(fields)),
        memory_integrity.TELEMETRY_MAGIC,
        *fields,
    )
    crc = (zlib.crc32(without_crc[:-4]) + crc_delta) & 0xFFFFFFFF
    fields[-1] = crc
    return struct.pack(
        "<4s" + ("I" * len(fields)),
        memory_integrity.TELEMETRY_MAGIC,
        *fields,
    )


def test_telemetry_decoder_and_bad_crc(tmp_path: Path) -> None:
    telemetry = tmp_path / "telemetry.bin"
    telemetry.write_bytes(telemetry_blob())
    report = tmp_path / "telemetry.json"

    result = run_tool(
        "decode-telemetry",
        "--input",
        str(telemetry),
        "--json-output",
        str(report),
    )

    assert result.returncode == 0, result.stdout + result.stderr
    data = load(report)
    assert data["trusted_security_state"]["selected_slot"] == 3
    assert "reset_cause" in data["untrusted_diagnostic_observations"]

    telemetry.write_bytes(telemetry_blob(crc_delta=1))
    assert run_tool(
        "decode-telemetry",
        "--input",
        str(telemetry),
        "--json-output",
        str(report),
    ).returncode == 1
    assert "bad report CRC" in load(report)["error"]


def test_malformed_telemetry(tmp_path: Path) -> None:
    telemetry = tmp_path / "telemetry.bin"
    telemetry.write_bytes(b"short")
    report = tmp_path / "telemetry.json"

    result = run_tool(
        "decode-telemetry",
        "--input",
        str(telemetry),
        "--json-output",
        str(report),
    )

    assert result.returncode == 1
    assert "malformed telemetry length" in load(report)["error"]
