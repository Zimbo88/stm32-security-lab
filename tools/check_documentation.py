#!/usr/bin/env python3
"""Check local Markdown links without requiring network access."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK_RE = re.compile(r"!?(?:\[[^\]]*\])\(([^)]+)\)")


def markdown_files(paths: list[Path]) -> list[Path]:
    result: list[Path] = []
    for path in paths:
        if path.is_file() and path.suffix.lower() == ".md":
            result.append(path)
        elif path.is_dir():
            result.extend(item for item in path.rglob("*.md") if item.is_file())
    return sorted(set(result))


def check_file(path: Path) -> list[dict[str, object]]:
    findings: list[dict[str, object]] = []
    text = path.read_text(encoding="utf-8")
    for line_number, line in enumerate(text.splitlines(), start=1):
        for match in LINK_RE.finditer(line):
            target = match.group(1).strip().split()[0].strip("<>")
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target_path = target.split("#", 1)[0]
            if not target_path:
                continue
            resolved = (path.parent / target_path).resolve()
            if not resolved.is_file():
                findings.append(
                    {
                        "path": path.relative_to(ROOT).as_posix(),
                        "line": line_number,
                        "target": target,
                    }
                )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        default=[Path("README.md"), Path("docs"), Path(".github"), Path("CONTRIBUTING.md"), Path("SECURITY.md"), Path("CHANGELOG.md"), Path("CODE_OF_CONDUCT.md")],
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    files = markdown_files([(value if value.is_absolute() else ROOT / value) for value in args.paths])
    findings = [finding for path in files for finding in check_file(path)]
    report = {
        "schema_version": 1,
        "scope": [path.relative_to(ROOT).as_posix() for path in files],
        "missing_links": findings,
        "result": "ok" if not findings else "failed",
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(json.dumps({"result": report["result"], "missing_links": len(findings)}, sort_keys=True))
    return 0 if not findings else 1


if __name__ == "__main__":
    raise SystemExit(main())
