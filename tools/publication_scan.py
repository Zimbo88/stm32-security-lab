#!/usr/bin/env python3
"""Scan a repository or candidate directory for publication hazards."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess  # nosec B404 - fixed git query without shell execution
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
MAX_TEXT_BYTES = 2 * 1024 * 1024

PATTERNS: tuple[tuple[str, str, str], ...] = (
    ("error", "private-key-material", r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----"),
    ("error", "secret-token", r"\b(?:ghp|github_pat|AKIA)[A-Za-z0-9_\-]{16,}\b"),
    ("error", "absolute-home-path", r"/(?:home|Users)/[A-Za-z0-9._-]+/"),
    (
        "error",
        "private-seed-file",
        r"(?:^|/)(?:[^/]*(?:private|signing)[^/]*\.(?:seed|key)|[^/]+\.(?:seed|key))$",
    ),
    ("error", "hardware-serial", r"\b(?:ST-?LINK|serial)[ _:-]+[0-9A-F]{12,}\b"),
    ("warning", "local-device-path", r"/dev/(?:ttyUSB|ttyACM)\d+"),
    ("warning", "hardware-dump-extension", r"\.(?:dmp|dump|core|sr)$"),
    ("warning", "raw-local-log", r"(?:^|/)(?:hil-results|logs|test-results)/"),
    (
        "warning",
        "option-byte-write-command",
        r"(?i:(?:option.?byte|rdp|wrp).{0,80}(?:write|program|set))",
    ),
)


def tracked_paths() -> list[Path]:
    result = subprocess.run(  # nosec B603,B607 - fixed git argv and repository cwd
        ["git", "ls-files"], cwd=ROOT, check=True, capture_output=True, text=True
    )
    return [ROOT / line for line in result.stdout.splitlines() if line]


def relative(path: Path, base: Path) -> str:
    try:
        return path.relative_to(base).as_posix()
    except ValueError:
        return path.as_posix()


def load_allowlist(path: Path | None) -> set[tuple[str, str]]:
    if path is None or not path.exists():
        return set()
    value = json.loads(path.read_text(encoding="utf-8"))
    return {(str(item["kind"]), str(item["path"])) for item in value.get("allow", [])}


def scan_file(path: Path, base: Path, allow: set[tuple[str, str]]) -> list[dict[str, Any]]:
    path_name = relative(path, base)
    findings: list[dict[str, Any]] = []
    name_text = path_name.replace("\\", "/")
    for severity, kind, expression in PATTERNS:
        match = re.search(expression, name_text)
        if match is not None and (kind, path_name) not in allow:
            findings.append({"severity": severity, "kind": kind, "path": path_name, "line": 0})
    try:
        data = path.read_bytes()
    except OSError:
        return findings
    if len(data) > MAX_TEXT_BYTES or b"\x00" in data[:4096]:
        return findings
    text = data.decode("utf-8", errors="replace")
    for line_number, line in enumerate(text.splitlines(), start=1):
        for severity, kind, expression in PATTERNS:
            # Filename rules are evaluated against path names above.  Applying
            # them to prose would flag safe examples such as ``*.seed`` in
            # .gitignore and documentation; actual key blocks remain errors.
            if kind == "private-seed-file":
                continue
            if re.search(expression, line) and (kind, path_name) not in allow:
                findings.append({"severity": severity, "kind": kind, "path": path_name, "line": line_number})
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--tracked", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allowlist", type=Path)
    args = parser.parse_args()
    if args.tracked:
        paths = tracked_paths()
        base = ROOT
    else:
        paths = []
        for value in args.paths or [ROOT]:
            candidate = value.resolve()
            paths.extend(p for p in candidate.rglob("*") if p.is_file())
        base = (args.paths[0].resolve() if args.paths else ROOT)
    allow = load_allowlist(args.allowlist)
    findings = []
    for path in sorted(set(paths)):
        findings.extend(scan_file(path, base, allow))
    unique = {(item["severity"], item["kind"], item["path"], item["line"]): item for item in findings}
    findings = [unique[key] for key in sorted(unique)]
    report = {
        "schema_version": 1,
        "result": "clean" if not any(item["severity"] == "error" for item in findings) else "failed",
        "scope": "tracked" if args.tracked else [path.as_posix() for path in (args.paths or [ROOT])],
        "allowlist_entries": len(allow),
        "findings": findings,
        "report_sha256": None,
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    report["report_sha256"] = hashlib.sha256(encoded.encode("ascii")).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="ascii")
    print(json.dumps({"result": report["result"], "findings": len(findings)}, sort_keys=True))
    return 0 if report["result"] == "clean" else 1


if __name__ == "__main__":
    raise SystemExit(main())
