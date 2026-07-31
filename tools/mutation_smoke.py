#!/usr/bin/env python3
"""Small mutation-testing prototype for the Python UART parser."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess  # nosec B404
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "tools" / "stm32ctl" / "protocol.py"
CHECK = """
from stm32ctl.protocol import MAX_PAYLOAD_SIZE, decode_frame, encode_frame

frame = encode_frame(1, 0, b"\\xA5" * MAX_PAYLOAD_SIZE)
assert decode_frame(frame).payload == b"\\xA5" * MAX_PAYLOAD_SIZE
"""


def run_mutant(name: str, old: str, new: str) -> dict[str, object]:
    original = PROTOCOL.read_text(encoding="utf-8")
    if old not in original:
        raise RuntimeError(f"mutation source text not found: {name}")
    with tempfile.TemporaryDirectory(prefix="stm32-mutation-") as temporary:
        package = Path(temporary) / "stm32ctl"
        shutil.copytree(ROOT / "tools" / "stm32ctl", package)
        mutated = original.replace(old, new, 1)
        (package / "protocol.py").write_text(mutated, encoding="utf-8")
        # The temporary package is first; repository tools satisfy its imports.
        environment = {"PYTHONPATH": f"{temporary}{os.pathsep}{ROOT / 'tools'}"}
        # The child receives a fixed test program and a temporary import path.
        completed = subprocess.run(  # nosec B603
            [sys.executable, "-c", CHECK],
            cwd=Path(temporary),
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )
    return {
        "mutation": name,
        "killed": completed.returncode != 0,
        "returncode": completed.returncode,
        "stderr": completed.stderr.strip(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args()
    results = [
        run_mutant(
            "reject-at-max-payload-boundary",
            "if payload_length > MAX_PAYLOAD_SIZE:",
            "if payload_length >= MAX_PAYLOAD_SIZE:",
        ),
        run_mutant(
            "invert-magic-check",
            "if magic != MAGIC:",
            "if magic == MAGIC:",
        ),
    ]
    report = {"engine": "temporary-source-mutation", "results": results}
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.json_output is not None:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(encoded, encoding="ascii")
    print(encoded, end="")
    return 0 if all(result["killed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
