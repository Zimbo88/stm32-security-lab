from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from nacl.signing import SigningKey


ROOT = Path(__file__).resolve().parents[1]

sys.path.insert(0, str(ROOT / "tools"))
import memory_integrity  # noqa: E402
from stm32f429_layout import LAYOUT  # noqa: E402


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
        assert report["package"]["slot"] == slot
        assert report["package"]["vector_address"] == LAYOUT[f"slot_{slot}_payload_base"]
        assert report["verification"]["signature_valid"] is True
