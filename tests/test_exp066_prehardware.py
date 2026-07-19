from __future__ import annotations

import hashlib
import json
import subprocess
import struct
import sys
from pathlib import Path

from nacl.signing import SigningKey


ROOT = Path(__file__).resolve().parents[1]

sys.path.insert(0, str(ROOT / "tools"))
import memory_integrity  # noqa: E402
from stm32f429_layout import LAYOUT  # noqa: E402


def readelf_load_segments(elf: Path) -> list[dict[str, int]]:
    result = subprocess.run(
        ["arm-none-eabi-readelf", "-lW", str(elf)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    segments: list[dict[str, int]] = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if not parts or parts[0] != "LOAD":
            continue
        segments.append({
            "offset": int(parts[1], 16),
            "virt_addr": int(parts[2], 16),
            "phys_addr": int(parts[3], 16),
            "file_size": int(parts[4], 16),
            "mem_size": int(parts[5], 16),
        })
    assert segments
    return segments


def assert_no_flash_load_reaches_1m(elf: Path) -> None:
    for segment in readelf_load_segments(elf):
        load = segment["phys_addr"]
        load_end = load + segment["file_size"]
        assert not (LAYOUT["flash_end"] <= load < LAYOUT_PROFILES_LEGACY_END)
        if LAYOUT["flash_base"] <= load < LAYOUT["flash_end"]:
            assert load_end <= LAYOUT["flash_end"]


LAYOUT_PROFILES_LEGACY_END = 0x08200000


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_runtime_telemetry_layout_matches_host_decoder() -> None:
    header = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "experiment_telemetry.h"
    ).read_text(encoding="ascii")

    for field in memory_integrity.TELEMETRY_FIELD_NAMES:
        assert f"uint32_t {field};" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC0 'S'" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC1 'T'" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC2 'M'" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC3 'R'" in header


def test_confirmation_health_gate_and_existing_api_usage() -> None:
    source = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "platform_confirmation.c"
    ).read_text(encoding="ascii")

    for term in (
        "early_platform_init_done",
        "running_slot_identified",
        "core_self_checks_passed",
        "critical_initialization_failure",
        "stable_execution_point_reached",
        "metadata_allows_confirmation",
    ):
        assert term in source
    assert "boot_confirm_slot_from_vector_address" in source
    assert "boot_confirm_current_slot" in source
    assert "boot_flash_target_init_metadata" in source


def test_exp066_slot_a_and_slot_b_release_targets(tmp_path: Path) -> None:
    seed = bytes(32)
    seed_path = tmp_path / "test_seed.bin"
    seed_path.write_bytes(seed)
    public_key_hex = bytes(SigningKey(seed).verify_key).hex()

    bootloader = subprocess.run(
        [
            "make",
            "-C",
            "firmware/exp045_bootloader_v2",
            "clean",
            "all",
            "LAYOUT_PROFILE=stm32f429_1m",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert bootloader.returncode == 0, bootloader.stdout + bootloader.stderr
    bootloader_elf = (
        ROOT
        / "firmware"
        / "exp045_bootloader_v2"
        / "build"
        / "exp045_bootloader_v2.elf"
    )
    assert_no_flash_load_reaches_1m(bootloader_elf)

    result = subprocess.run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "slot-releases",
            f"SIGNING_SEED={seed_path}",
            f"PUBLIC_KEY_HEX={public_key_hex}",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    for slot in ("a", "b"):
        report_path = (
            ROOT
            / "firmware"
            / "exp066_research_platform_core"
            / "build"
            / f"slot_{slot}"
            / (
                "exp066_research_platform_core_"
                f"slot_{slot}_slot_{slot}_package_verify.json"
            )
        )
        report = json.loads(report_path.read_text(encoding="ascii"))
        assert report["result"] == "ok"
        assert report["target_layout"]["mcu"] == "STM32F429IGT6"
        assert report["target_layout"]["layout_profile"] == "stm32f429_1m"
        assert report["target_layout"]["flash_size"] == 0x00100000
        assert report["target_layout"]["sector_count"] == 12
        assert report["package"]["slot"] == slot
        assert report["package"]["vector_address"] == LAYOUT[f"slot_{slot}_payload_base"]
        assert report["verification"]["signature_valid"] is True

        elf = (
            ROOT
            / "firmware"
            / "exp066_research_platform_core"
            / "build"
            / f"slot_{slot}"
            / f"exp066_research_platform_core_slot_{slot}.elf"
        )
        binary = elf.with_suffix(".bin")
        assert_no_flash_load_reaches_1m(elf)
        assert len(binary.read_bytes()) <= LAYOUT[f"slot_{slot}_payload_max_size"]
        initial_msp, reset_vector = struct.unpack_from("<II", binary.read_bytes(), 0)
        reset_address = reset_vector & ~1
        assert initial_msp == LAYOUT["application_msp_end"]
        assert LAYOUT[f"slot_{slot}_payload_base"] <= reset_address < LAYOUT[f"slot_{slot}_end"]
        assert sha256(elf)
        assert sha256(binary)
