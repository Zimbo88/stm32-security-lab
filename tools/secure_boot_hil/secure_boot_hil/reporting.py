"""Machine-readable and human-readable HIL reports."""

from __future__ import annotations

import csv
import html
import json
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from typing import Any

from .config import HilConfig
from .logging import utc_now
from .metrics import aggregate_boot_metrics
from .model import MutationRecord, TestCase, TestResult, Verdict


def write_json(path: Path, payload: dict[str, Any] | list[Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def result_counts(results: list[TestResult]) -> dict[str, int]:
    counter = Counter(result.verdict.value for result in results)
    return {verdict.value: counter.get(verdict.value, 0) for verdict in Verdict}


def suite_exit_code(
    results: list[TestResult],
    *,
    restore_verified: bool,
    reports_written: bool,
    strict_observations: bool,
) -> int:
    if not restore_verified or not reports_written:
        return 1
    verdicts = [result.verdict for result in results]
    if any(verdict in (Verdict.FAIL, Verdict.ERROR) for verdict in verdicts):
        return 1
    if strict_observations and any(verdict == Verdict.OBSERVE for verdict in verdicts):
        return 1
    return 0


class Reporter:
    def __init__(self, run_dir: Path) -> None:
        self.run_dir = run_dir

    def write_static_inputs(
        self,
        *,
        config: HilConfig,
        environment: dict[str, Any],
        tests: list[TestCase],
    ) -> None:
        write_json(self.run_dir / "configuration.json", config.to_json())
        write_json(self.run_dir / "environment.json", environment)
        write_json(self.run_dir / "test-plan.json", [test.to_json() for test in tests])

    def write_results(
        self,
        *,
        results: list[TestResult],
        mutations: list[MutationRecord],
        restore_verified: bool,
        suite_start_utc: str,
        suite_end_utc: str | None = None,
    ) -> None:
        suite_end = suite_end_utc or utc_now()
        payload = {
            "schema_version": 1,
            "suite_start_utc": suite_start_utc,
            "suite_end_utc": suite_end,
            "restore_verified": restore_verified,
            "counts": result_counts(results),
            "results": [result.to_json() for result in results],
        }
        write_json(self.run_dir / "results.json", payload)
        self._write_csv(results)
        self._write_markdown(results, restore_verified=restore_verified)
        self._write_html(results, restore_verified=restore_verified)
        self._write_junit(results)
        write_json(
            self.run_dir / "performance.json",
            {
                "schema_version": 1,
                "statistics": aggregate_boot_metrics(
                    result.metrics for result in results if result.verdict == Verdict.PASS
                ),
                "note": (
                    "Firmware-reported timing, host reset-to-UART duration, flash "
                    "programming duration, and total test duration are distinct measurements."
                ),
            },
        )
        write_json(
            self.run_dir / "mutation-manifest.json",
            {
                "schema_version": 1,
                "mutations": [mutation.to_json() for mutation in mutations],
            },
        )

    def _write_csv(self, results: list[TestResult]) -> None:
        path = self.run_dir / "results.csv"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=[
                    "identifier",
                    "name",
                    "verdict",
                    "duration_seconds",
                    "error",
                    "uart_frame",
                ],
            )
            writer.writeheader()
            for result in results:
                writer.writerow(
                    {
                        "identifier": result.test.identifier,
                        "name": result.test.name,
                        "verdict": result.verdict.value,
                        "duration_seconds": f"{result.duration_seconds:.6f}",
                        "error": result.error or "",
                        "uart_frame": result.uart_frame.path.as_posix()
                        if result.uart_frame and result.uart_frame.path
                        else "",
                    }
                )

    def _write_markdown(self, results: list[TestResult], *, restore_verified: bool) -> None:
        lines = [
            "# Secure Boot HIL Report",
            "",
            f"Generated UTC: {utc_now()}",
            f"Restore verified: {restore_verified}",
            "",
            "## Verdict Counts",
            "",
        ]
        counts = result_counts(results)
        for verdict in Verdict:
            lines.append(f"- {verdict.value}: {counts[verdict.value]}")
        lines.extend(["", "## Tests", ""])
        for result in results:
            lines.extend(
                [
                    f"### {result.test.identifier} - {result.test.name}",
                    "",
                    f"- Verdict: {result.verdict.value}",
                    f"- Objective: {result.test.objective}",
                    f"- Duration: {result.duration_seconds:.3f} s",
                ]
            )
            if result.error:
                lines.append(f"- Error: {result.error}")
            if result.uart_frame and result.uart_frame.path:
                lines.append(f"- UART frame: `{result.uart_frame.path}`")
            lines.append("")
        (self.run_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")

    def _write_html(self, results: list[TestResult], *, restore_verified: bool) -> None:
        rows = []
        for result in results:
            rows.append(
                "<tr>"
                f"<td>{html.escape(result.test.identifier)}</td>"
                f"<td>{html.escape(result.test.name)}</td>"
                f"<td>{html.escape(result.verdict.value)}</td>"
                f"<td>{result.duration_seconds:.3f}</td>"
                f"<td>{html.escape(result.error or '')}</td>"
                "</tr>"
            )
        document = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Secure Boot HIL Report</title>
  <style>
    body {{ font-family: system-ui, sans-serif; margin: 2rem; color: #1f2933; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ border: 1px solid #cbd5e1; padding: 0.4rem; text-align: left; }}
    th {{ background: #f1f5f9; }}
  </style>
</head>
<body>
  <h1>Secure Boot HIL Report</h1>
  <p>Generated UTC: {html.escape(utc_now())}</p>
  <p>Restore verified: {html.escape(str(restore_verified))}</p>
  <table>
    <thead><tr><th>ID</th><th>Name</th><th>Verdict</th><th>Duration</th><th>Error</th></tr></thead>
    <tbody>{"".join(rows)}</tbody>
  </table>
</body>
</html>
"""
        (self.run_dir / "report.html").write_text(document, encoding="utf-8")

    def _write_junit(self, results: list[TestResult]) -> None:
        testsuite = ET.Element(
            "testsuite",
            {
                "name": "secure_boot_hil",
                "tests": str(len(results)),
                "failures": str(sum(1 for result in results if result.verdict == Verdict.FAIL)),
                "errors": str(sum(1 for result in results if result.verdict == Verdict.ERROR)),
                "skipped": str(
                    sum(
                        1 for result in results if result.verdict in (Verdict.SKIP, Verdict.OBSERVE)
                    )
                ),
            },
        )
        for result in results:
            testcase = ET.SubElement(
                testsuite,
                "testcase",
                {
                    "classname": "secure_boot_hil",
                    "name": result.test.identifier,
                    "time": f"{result.duration_seconds:.6f}",
                },
            )
            if result.verdict == Verdict.FAIL:
                failure = ET.SubElement(
                    testcase, "failure", {"message": result.error or "assertion failed"}
                )
                failure.text = json.dumps(result.to_json(), sort_keys=True)
            elif result.verdict == Verdict.ERROR:
                error = ET.SubElement(
                    testcase, "error", {"message": result.error or "execution error"}
                )
                error.text = json.dumps(result.to_json(), sort_keys=True)
            elif result.verdict in (Verdict.SKIP, Verdict.OBSERVE):
                skipped = ET.SubElement(testcase, "skipped", {"message": result.verdict.value})
                skipped.text = result.error or result.test.objective
        ET.ElementTree(testsuite).write(
            self.run_dir / "junit.xml",
            encoding="utf-8",
            xml_declaration=True,
        )
