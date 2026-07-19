import json
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "build" / "boot_metadata_provision.bin"

sys.path.insert(0, str(ROOT / "tools"))
from stm32f429_layout import LAYOUT  # noqa: E402


def build_tool() -> None:
    subprocess.run(
        ["make", "-C", str(ROOT / "tools"), "boot-metadata-provision"],
        check=True,
        text=True,
        capture_output=True,
    )


def run_tool(*args: str) -> subprocess.CompletedProcess[str]:
    build_tool()
    return subprocess.run(
        [str(TOOL), *args],
        check=False,
        text=True,
        capture_output=True,
    )


def le32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def test_create_confirmed_metadata_records_are_deterministic(tmp_path: Path) -> None:
    copy_a = tmp_path / "metadata_a.bin"
    copy_b = tmp_path / "metadata_b.bin"
    report = tmp_path / "report.json"

    result = run_tool(
        "create-confirmed",
        "--slot",
        "a",
        "--image-version",
        "2",
        "--copy-a-output",
        str(copy_a),
        "--copy-b-output",
        str(copy_b),
        "--json-output",
        str(report),
    )
    assert result.returncode == 0, result.stdout + result.stderr

    a = copy_a.read_bytes()
    b = copy_b.read_bytes()
    assert a == b
    assert len(a) == LAYOUT["boot_metadata_record_size"]
    assert le32(a, 0) == 0x314D5442
    assert le32(a, 4) == LAYOUT["boot_metadata_format_version"]
    assert le32(a, 8) == LAYOUT["boot_metadata_record_size"]
    assert le32(a, 12) == 1
    assert le32(a, 16) == 4
    assert le32(a, 20) == LAYOUT["slot_a"]["id"]
    assert le32(a, 24) == 0xFFFFFFFF
    assert le32(a, 28) == 2
    assert le32(a, 32) == 0
    assert le32(a, 36) == 1

    data = json.loads(report.read_text(encoding="ascii"))
    assert data["result"] == "ok"
    assert data["operation"] == "create-confirmed"
    assert data["layout_profile"] == "stm32f429_1m"
    assert data["copy_a_address"] == LAYOUT["boot_metadata_a_base"]
    assert data["copy_b_address"] == LAYOUT["boot_metadata_b_base"]
    assert data["output_size"] == LAYOUT["boot_metadata_record_size"]


def test_create_confirmed_sector_images_keep_erased_padding(tmp_path: Path) -> None:
    copy_a = tmp_path / "metadata_a_sector.bin"
    copy_b = tmp_path / "metadata_b_sector.bin"

    result = run_tool(
        "create-confirmed",
        "--slot",
        "b",
        "--image-version",
        "7",
        "--copy-a-output",
        str(copy_a),
        "--copy-b-output",
        str(copy_b),
        "--sector-image",
    )
    assert result.returncode == 0, result.stdout + result.stderr

    a = copy_a.read_bytes()
    b = copy_b.read_bytes()
    record_size = LAYOUT["boot_metadata_record_size"]
    assert len(a) == LAYOUT["boot_metadata_a_size"]
    assert len(b) == LAYOUT["boot_metadata_b_size"]
    assert a[:record_size] == b[:record_size]
    assert set(a[record_size:]) == {0xFF}
    assert set(b[record_size:]) == {0xFF}
    assert le32(a, 20) == LAYOUT["slot_b"]["id"]
    assert le32(a, 28) == 7


def test_create_update_sequence_rejects_non_advancing_candidate(
    tmp_path: Path,
) -> None:
    result = run_tool(
        "create-update-sequence",
        "--active-slot",
        "a",
        "--active-version",
        "3",
        "--candidate-slot",
        "b",
        "--candidate-version",
        "3",
        "--confirmed-output",
        str(tmp_path / "confirmed.bin"),
        "--writing-output",
        str(tmp_path / "writing.bin"),
        "--candidate-ready-output",
        str(tmp_path / "ready.bin"),
    )
    assert result.returncode != 0
    assert "failed to create update metadata sequence" in result.stderr
