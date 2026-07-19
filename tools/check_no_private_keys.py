#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PRIVATE_KEY_PATTERNS = (
    re.compile(b"-----BEGIN " + b"[A-Z0-9 ]*PRIVATE KEY" + b"-----"),
    re.compile(b"-----BEGIN " + b"OPENSSH PRIVATE KEY" + b"-----"),
    re.compile(b"-----BEGIN " + b"ENCRYPTED PRIVATE KEY" + b"-----"),
)

SENSITIVE_PATH_PATTERNS = (
    re.compile(r"(^|/)firmware_signing_seed\.bin$"),
    re.compile(r"(^|/).*_private\.pem$"),
    re.compile(r"(^|/).*\.key$"),
)

ALLOWED_PATHS = {
    "firmware/exp065_signed_app/keys/README.md",
    "logs/exp028_final_audit/private_key_git_check.txt",
}


def tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [
        ROOT / item.decode("utf-8")
        for item in result.stdout.split(b"\0")
        if item
    ]


def main() -> int:
    findings: list[str] = []

    for path in tracked_files():
        relative = path.relative_to(ROOT).as_posix()
        if relative not in ALLOWED_PATHS:
            for pattern in SENSITIVE_PATH_PATTERNS:
                if pattern.search(relative):
                    findings.append(f"sensitive tracked path: {relative}")

        try:
            data = path.read_bytes()
        except OSError as exc:
            findings.append(f"unable to read tracked file {relative}: {exc}")
            continue

        for pattern in PRIVATE_KEY_PATTERNS:
            if pattern.search(data):
                findings.append(f"private key material pattern in: {relative}")

    if findings:
        for finding in findings:
            print(finding)
        return 1

    print("No tracked private key patterns found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
