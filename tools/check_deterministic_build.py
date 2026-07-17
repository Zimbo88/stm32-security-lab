#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import subprocess
import tarfile
import tempfile
from io import BytesIO
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEST_SEED = bytes(range(32))

BUILD_OUTPUTS = (
    "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin",
    "firmware/exp065_signed_app/build/exp065_signed_app.bin",
    "firmware/exp065_signed_app/build/exp065_signed_app_signed.bin",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core.bin",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core_signed.bin",
)


def run(command: list[str], cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def export_head(destination: Path) -> None:
    archive = subprocess.run(
        ["git", "archive", "--format=tar", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    ).stdout

    with tarfile.open(fileobj=BytesIO(archive), mode="r:") as tar:
        tar.extractall(destination)


def build_checkout(checkout: Path) -> dict[str, str]:
    seed = checkout / ".deterministic_test_seed.bin"
    seed.write_bytes(TEST_SEED)

    run(["make", "-C", "firmware/exp045_bootloader_v2", "clean", "all"], checkout)
    run(["make", "-C", "firmware/exp065_signed_app", "clean", "all"], checkout)
    run(
        [
            "make",
            "-C",
            "firmware/exp065_signed_app",
            "signed",
            f"SIGNING_SEED={seed}",
        ],
        checkout,
    )
    run(["make", "-C", "firmware/exp066_research_platform_core", "clean", "all"], checkout)
    run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "signed",
            f"SIGNING_SEED={seed}",
        ],
        checkout,
    )

    return {
        output: hashlib.sha256((checkout / output).read_bytes()).hexdigest()
        for output in BUILD_OUTPUTS
    }


def main() -> int:
    with tempfile.TemporaryDirectory() as left_name, tempfile.TemporaryDirectory() as right_name:
        left = Path(left_name)
        right = Path(right_name)
        export_head(left)
        export_head(right)

        left_hashes = build_checkout(left)
        right_hashes = build_checkout(right)

    if left_hashes != right_hashes:
        print("Deterministic build comparison failed.")
        for output in BUILD_OUTPUTS:
            print(f"{output}: {left_hashes.get(output)} {right_hashes.get(output)}")
        return 1

    print("Deterministic build comparison passed.")
    for output in BUILD_OUTPUTS:
        print(f"{left_hashes[output]}  {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
