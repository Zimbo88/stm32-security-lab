#!/usr/bin/env python3
"""Run available local analyzers without including third-party crypto sources."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess  # nosec B404
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOOT = ROOT / "firmware" / "exp045_bootloader_v2"
COMMON = ROOT / "firmware" / "common"
C_SOURCES = (
    BOOT / "src" / "boot_flash.c",
    BOOT / "src" / "boot_metadata.c",
    BOOT / "src" / "boot_slot_selection.c",
    BOOT / "src" / "update_package.c",
    BOOT / "src" / "update_protocol.c",
)


def run(command: list[str]) -> dict[str, object]:
    completed = subprocess.run(  # nosec B603
        command, cwd=ROOT, capture_output=True, text=True, check=False
    )
    return {
        "command": command,
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json-output", type=Path, default=ROOT / "fuzz" / "findings-local" / "static-analysis.json")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    include = [f"-I{BOOT / 'src'}", f"-I{BOOT / 'include'}", f"-I{COMMON}"]
    gcc_command = [
        "gcc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wpointer-arith",
        "-Wcast-align",
        "-DBOOT_FLASH_TARGET_HOST_TEST",
        "-DSIGNED_IMAGE_HOST_TEST",
        "-fanalyzer",
        "-fsyntax-only",
        *include,
        *map(str, C_SOURCES),
    ]
    results: list[dict[str, object]] = []
    results.append({"tool": "gcc-fanalyzer", **run(gcc_command)})
    optional = {
        "cppcheck": ["cppcheck", "--enable=warning,style,performance,portability", *map(str, C_SOURCES)],
        "clang-tidy": ["clang-tidy", *map(str, C_SOURCES), "--", *include],
        "scan-build": ["scan-build", "gcc", *include, "-fsyntax-only", *map(str, C_SOURCES)],
    }
    missing: list[str] = []
    for name, command in optional.items():
        if shutil.which(command[0]) is None:
            missing.append(name)
            continue
        results.append({"tool": name, **run(command)})

    report = {"results": results, "missing_optional_tools": missing}
    args.json_output.parent.mkdir(parents=True, exist_ok=True)
    args.json_output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    if args.strict and missing:
        return 2
    return 0 if all(result["returncode"] == 0 for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
