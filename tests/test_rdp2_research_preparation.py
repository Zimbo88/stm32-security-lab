import json
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import rdp2_marker  # noqa: E402

REQUIRED_MARKERS = {
    "RDP2-BOOT-MARKER-01",
    "RDP2-SLOT-A-MARKER-01",
    "RDP2-SLOT-B-MARKER-01",
    "RDP2-SRAM-MARKER-01",
    "RDP2-CONFIG-MARKER-01",
}


def test_marker_spec_is_synthetic_and_complete():
    spec = rdp2_marker.load_spec(ROOT / "config" / "rdp2_research_markers.json")
    markers = {marker["id"]: marker for marker in spec["markers"]}
    assert set(markers) == REQUIRED_MARKERS
    for marker in markers.values():
        data = rdp2_marker.marker_bytes(marker)
        assert data.endswith(b"\0")
        assert len(data) == len(marker["value"]) + 1
        assert rdp2_marker.sha256_hex(data)
    assert markers["RDP2-CONFIG-MARKER-01"]["target_installation"] == "not-installed"


def test_marker_scanner_reports_full_and_partial_matches(tmp_path):
    spec = rdp2_marker.load_spec(ROOT / "config" / "rdp2_research_markers.json")
    marker = next(item for item in spec["markers"] if item["id"] == "RDP2-BOOT-MARKER-01")
    expected = rdp2_marker.marker_bytes(marker)
    dump = tmp_path / "partial-and-full.dump"
    dump.write_bytes(b"prefix" + expected[:8] + b"middle" + expected + b"suffix")

    report = rdp2_marker.scan_dump(dump, spec)
    boot = next(item for item in report["markers"] if item["id"] == marker["id"])
    assert boot["full_matches"] == [len(b"prefix") + 8 + len(b"middle")]
    assert boot["partial_prefix_matches"]["8"] == [
        len(b"prefix"),
        len(b"prefix") + 8 + len(b"middle"),
    ]


def test_manual_uart_procedure_is_debugger_independent():
    document = (ROOT / "docs" / "rdp2-uart-only-hardware-test.md").read_text(encoding="utf-8")
    for required in (
        "USART1 TX",
        "USART1 RX",
        "stm32ctl",
        "Slot A",
        "Slot B",
        "Ungültiges Paket",
        "Abbruch",
        "startet weiterhin die letzte bestätigte Firmware",
    ):
        assert required in document
    for forbidden in ("SWD", "JTAG", "OpenOCD", "GDB", "ST-Link", "ROM-Systembootloader"):
        assert forbidden in document


def test_no_automatic_rdp2_target_or_option_byte_writer():
    makefiles = list(ROOT.rglob("Makefile"))
    for makefile in makefiles:
        text = makefile.read_text(encoding="utf-8", errors="replace")
        assert not re.search(r"(?im)^\s*(?:rdp2|enable-rdp2)\s*:", text)

    source_files = list((ROOT / "tools").rglob("*.py")) + list((ROOT / "firmware").rglob("*.c"))
    for source in source_files:
        text = source.read_text(encoding="utf-8", errors="replace")
        assert not re.search(r"(?i)\b(?:RDP|read[_ -]?protection)\s*[=:]\s*2\b", text)
        assert "STM32CubeProgrammer" not in text


def test_baseline_output_is_ignored_and_declares_no_private_key():
    gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
    assert "baseline/rdp2-final/" in gitignore
    manifest_path = ROOT / "baseline" / "rdp2-final" / "release-manifest.json"
    if not manifest_path.is_file():
        pytest.skip("ignored local baseline is not present in a fresh checkout")
    manifest = json.loads(manifest_path.read_text(encoding="ascii"))
    assert manifest["private_signing_key"] == {
        "copied": False,
        "printed": False,
        "operator_managed_external_backup_required": True,
    }
