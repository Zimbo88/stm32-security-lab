#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import subprocess
import tarfile
import tempfile
from io import BytesIO
from pathlib import Path

from nacl.signing import SigningKey


ROOT = Path(__file__).resolve().parents[1]
TEST_SEED = bytes(range(32))
TEST_PUBLIC_KEY_HEX = bytes(SigningKey(TEST_SEED).verify_key).hex()

BUILD_OUTPUTS = (
    "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.elf",
    "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin",
    "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.hex",
    "firmware/exp065_signed_app/build/exp065_signed_app.elf",
    "firmware/exp065_signed_app/build/exp065_signed_app.bin",
    "firmware/exp065_signed_app/build/exp065_signed_app.hex",
    "firmware/exp065_signed_app/build/exp065_signed_app_signed.bin",
    "firmware/exp065_signed_app/build/exp065_release_manifest.json",
    "firmware/exp065_signed_app/build/exp065_release_verification.json",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core.elf",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core.bin",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core.hex",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core_signed.bin",
    "firmware/exp066_research_platform_core/build/exp066_release_manifest.json",
    "firmware/exp066_research_platform_core/build/exp066_release_verification.json",
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


def head_commit() -> str:
    return subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()


def verify_release(
    checkout: Path,
    *,
    git_commit: str,
    application: str,
    signed_image: str,
    manifest_output: str,
    report_output: str,
) -> None:
    project = f"firmware/{application}"
    run(
        [
            "python3",
            "tools/release_artifacts.py",
            "verify-release",
            "--bootloader-elf",
            "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.elf",
            "--bootloader-bin",
            "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin",
            "--bootloader-hex",
            "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.hex",
            "--application-elf",
            f"{project}/build/{application}.elf",
            "--application-bin",
            f"{project}/build/{application}.bin",
            "--application-hex",
            f"{project}/build/{application}.hex",
            "--signed-image",
            signed_image,
            "--public-key-hex",
            TEST_PUBLIC_KEY_HEX,
            "--manifest-output",
            manifest_output,
            "--report-output",
            report_output,
            "--application-name",
            application,
            "--bootloader-version",
            "exp045",
            "--release-version",
            "deterministic-test",
            "--git-commit",
            git_commit,
        ],
        checkout,
    )


def build_checkout(checkout: Path) -> dict[str, str]:
    seed = checkout / ".deterministic_test_seed.bin"
    seed.write_bytes(TEST_SEED)
    git_commit = head_commit()

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
    verify_release(
        checkout,
        git_commit=git_commit,
        application="exp065_signed_app",
        signed_image="firmware/exp065_signed_app/build/exp065_signed_app_signed.bin",
        manifest_output="firmware/exp065_signed_app/build/exp065_release_manifest.json",
        report_output="firmware/exp065_signed_app/build/exp065_release_verification.json",
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
    verify_release(
        checkout,
        git_commit=git_commit,
        application="exp066_research_platform_core",
        signed_image=(
            "firmware/exp066_research_platform_core/build/"
            "exp066_research_platform_core_signed.bin"
        ),
        manifest_output=(
            "firmware/exp066_research_platform_core/build/"
            "exp066_release_manifest.json"
        ),
        report_output=(
            "firmware/exp066_research_platform_core/build/"
            "exp066_release_verification.json"
        ),
    )

    return {
        output: hashlib.sha256((checkout / output).read_bytes()).hexdigest()
        for output in BUILD_OUTPUTS
    }


def mismatched_outputs(left_hashes: dict[str, str], right_hashes: dict[str, str]) -> list[str]:
    return [
        output
        for output in BUILD_OUTPUTS
        if left_hashes.get(output) != right_hashes.get(output)
    ]


def main() -> int:
    with tempfile.TemporaryDirectory() as left_name, tempfile.TemporaryDirectory() as right_name:
        left = Path(left_name)
        right = Path(right_name)
        export_head(left)
        export_head(right)

        left_hashes = build_checkout(left)
        right_hashes = build_checkout(right)

    mismatches = mismatched_outputs(left_hashes, right_hashes)
    if mismatches:
        print("Deterministic build comparison failed.")
        for output in mismatches:
            print(f"{output}: {left_hashes.get(output)} {right_hashes.get(output)}")
        return 1

    print("Deterministic build comparison passed.")
    for output in BUILD_OUTPUTS:
        print(f"{left_hashes[output]}  {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
