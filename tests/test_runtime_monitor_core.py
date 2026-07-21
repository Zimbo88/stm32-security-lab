from __future__ import annotations

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_runtime_monitor_core_host_harness() -> None:
    result = subprocess.run(
        ["make", "-C", "tests/rsm_core", "clean", "test"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_rsm_cli_policy_surface_guardrails() -> None:
    platform = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "platform.c"
    ).read_text(encoding="ascii")
    runtime_monitor = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "runtime_monitor.c"
    ).read_text(encoding="ascii")

    assert 'eq(command, "rsm status")' in platform
    assert 'eq(command, "rsm status public")' in platform
    assert 'eq(command, "rsm status restricted")' in platform
    assert 'eq(command, "secret' not in platform.lower()
    assert "RSM_EVENT_CORE_RUNTIME_READY" not in runtime_monitor
    assert "RSM_EVENT_EVIDENCE_RUNTIME_READY" in runtime_monitor
