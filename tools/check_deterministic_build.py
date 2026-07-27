#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
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
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core_slot_a_update_v2.bin",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core_slot_a_package_build.json",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core_slot_a_release_manifest.json",
    "firmware/exp066_research_platform_core/build/exp066_research_platform_core_slot_a_package_verify.json",
    "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a.elf",
    "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a.bin",
    "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a.hex",
    "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_update_v2.bin",
    "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_release_manifest.json",
    "firmware/exp066_research_platform_core/build/slot_a/exp066_research_platform_core_slot_a_slot_a_package_verify.json",
    "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b.elf",
    "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b.bin",
    "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b.hex",
    "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_update_v2.bin",
    "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_release_manifest.json",
    "firmware/exp066_research_platform_core/build/slot_b/exp066_research_platform_core_slot_b_slot_b_package_verify.json",
)

VOLATILE_JSON_FIELDS_BY_SUFFIX = {
    "_package_verify.json": ("verification_timestamp_utc",),
}


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

    run(
        [
            "make",
            "-C",
            "firmware/exp045_bootloader_v2",
            "clean",
            "all",
            "LAYOUT_PROFILE=stm32f429_1m",
        ],
        checkout,
    )
    run(
        [
            "make",
            "-C",
            "firmware/exp065_signed_app",
            "clean",
            "all",
            "LAYOUT_PROFILE=stm32f429_1m",
        ],
        checkout,
    )
    run(
        [
            "make",
            "-C",
            "firmware/exp065_signed_app",
            "signed",
            "LAYOUT_PROFILE=stm32f429_1m",
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
    run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "clean",
            "all",
            "LAYOUT_PROFILE=stm32f429_1m",
        ],
        checkout,
    )
    run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "inspect-update-package",
            "LAYOUT_PROFILE=stm32f429_1m",
            f"SIGNING_SEED={seed}",
        ],
        checkout,
    )
    run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "verify-signed",
            "LAYOUT_PROFILE=stm32f429_1m",
            f"SIGNING_SEED={seed}",
            f"PUBLIC_KEY_HEX={TEST_PUBLIC_KEY_HEX}",
        ],
        checkout,
    )
    run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "slot-releases",
            "LAYOUT_PROFILE=stm32f429_1m",
            f"SIGNING_SEED={seed}",
            f"PUBLIC_KEY_HEX={TEST_PUBLIC_KEY_HEX}",
        ],
        checkout,
    )

    return {output: artifact_hash(checkout, output) for output in BUILD_OUTPUTS}


def normalized_artifact_bytes(checkout: Path, output: str) -> bytes:
    data = (checkout / output).read_bytes()

    for suffix, fields in VOLATILE_JSON_FIELDS_BY_SUFFIX.items():
        if output.endswith(suffix):
            report = json.loads(data.decode("ascii"))
            for field in fields:
                if field in report:
                    report[field] = "<normalized>"
            return json.dumps(report, indent=2, sort_keys=True).encode("ascii") + b"\n"

    return data


def artifact_hash(checkout: Path, output: str) -> str:
    return hashlib.sha256(normalized_artifact_bytes(checkout, output)).hexdigest()


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
