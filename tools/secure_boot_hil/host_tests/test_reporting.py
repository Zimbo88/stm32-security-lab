from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from pathlib import Path

from secure_boot_hil.config import load_config
from secure_boot_hil.model import TestResult, UartFrame, Verdict
from secure_boot_hil.reporting import Reporter, suite_exit_code
from secure_boot_hil.testspec import initial_catalog

ROOT = Path(__file__).resolve().parents[3]


def test_report_generation_and_junit_validity(tmp_path: Path) -> None:
    config = load_config(repo_root=ROOT, output_root=tmp_path / "out")
    test = initial_catalog()[0]
    result = TestResult(
        test=test,
        verdict=Verdict.PASS,
        required=[],
        forbidden=[],
        uart_frame=UartFrame("Verification = OK", True, 0, 17, tmp_path / "frame.txt"),
        metrics={"sha512_us": 1},
        start_utc="2026-01-01T00:00:00Z",
        end_utc="2026-01-01T00:00:01Z",
        duration_seconds=1.0,
    )
    reporter = Reporter(tmp_path)
    reporter.write_static_inputs(config=config, environment={"git": {}}, tests=[test])
    reporter.write_results(
        results=[result],
        mutations=[],
        restore_verified=True,
        suite_start_utc="2026-01-01T00:00:00Z",
    )
    assert json.loads((tmp_path / "results.json").read_text())["counts"]["PASS"] == 1
    assert (tmp_path / "report.html").read_text(encoding="utf-8").startswith("<!doctype html>")
    ET.parse(tmp_path / "junit.xml")


def test_suite_exit_code_rules() -> None:
    assert (
        suite_exit_code([], restore_verified=True, reports_written=True, strict_observations=False)
        == 0
    )
    assert (
        suite_exit_code([], restore_verified=False, reports_written=True, strict_observations=False)
        == 1
    )
