#!/usr/bin/env python3
"""Perform offline structural checks for repository GitHub Actions workflows."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml  # type: ignore[import-untyped]  # PyYAML has no bundled mypy stubs.

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    findings: list[str] = []
    workflows = sorted((ROOT / ".github/workflows").glob("*.y*ml"))
    for path in workflows:
        try:
            value = yaml.safe_load(path.read_text(encoding="utf-8"))
        except (OSError, yaml.YAMLError) as exc:
            findings.append(f"{path.relative_to(ROOT)}: YAML parse failed: {exc}")
            continue
        relative = path.relative_to(ROOT).as_posix()
        if not isinstance(value, dict) or not value.get("name") or not value.get("jobs"):
            findings.append(f"{relative}: workflow name and jobs are required")
            continue
        permissions = value.get("permissions")
        if not isinstance(permissions, dict) or permissions.get("contents") != "read":
            findings.append(f"{relative}: top-level contents: read permission is required")
        jobs = value["jobs"]
        if isinstance(jobs, dict):
            for name, job in jobs.items():
                if not isinstance(job, dict) or not isinstance(job.get("timeout-minutes"), int):
                    findings.append(f"{relative}:{name}: timeout-minutes is required")
                if isinstance(job, dict):
                    for step in job.get("steps", []):
                        if isinstance(step, dict) and isinstance(step.get("uses"), str):
                            action = step["uses"]
                            if "@" not in action or len(action.rsplit("@", 1)[1]) < 20:
                                findings.append(f"{relative}:{name}: action is not pinned: {action}")
    report = {"schema_version": 1, "workflows": [path.relative_to(ROOT).as_posix() for path in workflows], "findings": findings, "result": "ok" if not findings else "failed"}
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="ascii")
    print(text, end="")
    return 0 if not findings else 1


if __name__ == "__main__":
    raise SystemExit(main())
