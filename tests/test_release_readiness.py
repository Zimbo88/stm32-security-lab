from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run_tool(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, *arguments],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def test_sbom_generation_is_deterministic(tmp_path: Path) -> None:
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"
    for output in (first, second):
        result = run_tool(
            "tools/generate_sbom.py",
            "--output",
            str(output),
            "--release-version",
            "test",
            "--source-commit",
            "0" * 40,
        )
        assert result.returncode == 0, result.stderr
    assert first.read_bytes() == second.read_bytes()
    assert json.loads(first.read_text(encoding="ascii"))["spdxVersion"] == "SPDX-2.3"


def test_publication_scan_rejects_private_key_material(tmp_path: Path) -> None:
    candidate = tmp_path / "candidate"
    candidate.mkdir()
    (candidate / "bad.txt").write_text(
        "-----BEGIN PRIVATE KEY-----\n", encoding="ascii"
    )
    report = tmp_path / "report.json"
    result = run_tool("tools/publication_scan.py", str(candidate), "--output", str(report))
    assert result.returncode == 1
    assert json.loads(report.read_text(encoding="ascii"))["result"] == "failed"


def test_workflow_and_documentation_checks_pass() -> None:
    docs = run_tool("tools/check_documentation.py")
    workflows = run_tool("tools/check_workflows.py")
    assert docs.returncode == 0, docs.stdout + docs.stderr
    assert workflows.returncode == 0, workflows.stdout + workflows.stderr
