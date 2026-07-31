from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path
from typing import TypedDict

from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[1]
UPDATE_PACKAGE_MAGIC = 0x31474953
UPDATE_PACKAGE_FORMAT_VERSION = 2
UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1 = 0xF429AB01
UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION = 1

sys.path.insert(0, str(ROOT / "tools"))
import memory_integrity  # noqa: E402
from stm32f429_layout import LAYOUT  # noqa: E402


class SignedManifest(TypedDict):
    magic: int
    header_version: int
    image_version: int
    vector_address: int
    image_size: int
    flags: int
    reserved0: int
    reserved1: int
    payload_sha512: bytes
    total_size: int


def readelf_load_segments(elf: Path) -> list[dict[str, int]]:
    result = subprocess.run(
        ["arm-none-eabi-readelf", "-lW", str(elf)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    segments: list[dict[str, int]] = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if not parts or parts[0] != "LOAD":
            continue
        segments.append({
            "offset": int(parts[1], 16),
            "virt_addr": int(parts[2], 16),
            "phys_addr": int(parts[3], 16),
            "file_size": int(parts[4], 16),
            "mem_size": int(parts[5], 16),
        })
    assert segments
    return segments


def assert_no_flash_load_reaches_1m(elf: Path) -> None:
    for segment in readelf_load_segments(elf):
        load = segment["phys_addr"]
        load_end = load + segment["file_size"]
        assert not (LAYOUT["flash_end"] <= load < LAYOUT_PROFILES_LEGACY_END)
        if LAYOUT["flash_base"] <= load < LAYOUT["flash_end"]:
            assert load_end <= LAYOUT["flash_end"]


LAYOUT_PROFILES_LEGACY_END = 0x08200000


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decode_signed_manifest(path: Path) -> SignedManifest:
    data = path.read_bytes()
    (
        magic,
        header_version,
        image_version,
        vector_address,
        image_size,
        flags,
        reserved0,
        reserved1,
        payload_sha512,
    ) = struct.unpack_from("<8I64s", data, 0)
    return {
        "magic": magic,
        "header_version": header_version,
        "image_version": image_version,
        "vector_address": vector_address,
        "image_size": image_size,
        "flags": flags,
        "reserved0": reserved0,
        "reserved1": reserved1,
        "payload_sha512": payload_sha512,
        "total_size": len(data),
    }


def test_runtime_telemetry_layout_matches_host_decoder() -> None:
    header = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "experiment_telemetry.h"
    ).read_text(encoding="ascii")

    for field in memory_integrity.TELEMETRY_FIELD_NAMES:
        assert f"uint32_t {field};" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC0 'S'" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC1 'T'" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC2 'M'" in header
    assert "EXPERIMENT_TELEMETRY_MAGIC3 'R'" in header


def test_confirmation_health_gate_and_existing_api_usage() -> None:
    source = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "platform_confirmation.c"
    ).read_text(encoding="ascii")

    for term in (
        "early_platform_init_done",
        "running_slot_identified",
        "core_self_checks_passed",
        "critical_initialization_failure",
        "stable_execution_point_reached",
        "metadata_allows_confirmation",
    ):
        assert term in source
    assert "boot_confirm_slot_from_vector_address" in source
    assert "boot_confirm_current_slot" in source
    assert "boot_flash_target_init_metadata" in source


def test_rejected_candidate_version_is_not_reported_as_running_image() -> None:
    telemetry_source = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "experiment_telemetry.c"
    ).read_text(encoding="ascii")
    confirmation_source = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "src"
        / "platform_confirmation.c"
    ).read_text(encoding="ascii")

    assert "trusted_metadata_image_version" in telemetry_source
    assert "report.image_version = trusted_metadata_image_version(&metadata);" in telemetry_source
    assert "trusted_metadata_image_version" in confirmation_source
    assert (
        "last_snapshot.image_version = trusted_metadata_image_version(&metadata);"
        in confirmation_source
    )


def test_boot_policy_uses_metadata_writable_flash_backend() -> None:
    source = (
        ROOT
        / "firmware"
        / "exp045_bootloader_v2"
        / "src"
        / "boot_policy.c"
    ).read_text(encoding="ascii")
    body = source.split("boot_slot_selection_status_t boot_policy_select", 1)[1]

    assert "boot_flash_target_init_metadata(&flash)" in body
    assert "boot_flash_target_init_readonly(&flash)" not in body


def test_exp066_slot_a_and_slot_b_release_targets(tmp_path: Path) -> None:
    seed = bytes(32)
    seed_path = tmp_path / "test_seed.bin"
    seed_path.write_bytes(seed)
    public_key_hex = bytes(SigningKey(seed).verify_key).hex()

    bootloader = subprocess.run(
        [
            "make",
            "-C",
            "firmware/exp045_bootloader_v2",
            "clean",
            "all",
            "LAYOUT_PROFILE=stm32f429_1m",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert bootloader.returncode == 0, bootloader.stdout + bootloader.stderr
    bootloader_elf = (
        ROOT
        / "firmware"
        / "exp045_bootloader_v2"
        / "build"
        / "exp045_bootloader_v2.elf"
    )
    assert_no_flash_load_reaches_1m(bootloader_elf)

    result = subprocess.run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "slot-releases",
            f"SIGNING_SEED={seed_path}",
            f"PUBLIC_KEY_HEX={public_key_hex}",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    for slot in ("a", "b"):
        report_path = (
            ROOT
            / "firmware"
            / "exp066_research_platform_core"
            / "build"
            / f"slot_{slot}"
            / (
                "exp066_research_platform_core_"
                f"slot_{slot}_slot_{slot}_package_verify.json"
            )
        )
        report = json.loads(report_path.read_text(encoding="ascii"))
        assert report["result"] == "ok"
        assert report["target_layout"]["mcu"] == "STM32F429IGT6"
        assert report["target_layout"]["layout_profile"] == "stm32f429_1m"
        assert report["target_layout"]["flash_size"] == 0x00100000
        assert report["target_layout"]["sector_count"] == 12
        assert report["package"]["slot"] == slot
        assert report["package"]["format_version"] == UPDATE_PACKAGE_FORMAT_VERSION
        assert (
            report["package"]["target_compatibility"]
            == UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1
        )
        assert (
            report["package"]["image_type"]
            == UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION
        )
        assert report["package"]["vector_address"] == LAYOUT[f"slot_{slot}_payload_base"]
        assert report["manifest"]["header_version"] == UPDATE_PACKAGE_FORMAT_VERSION
        assert (
            report["manifest"]["reserved0"]
            == UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1
        )
        assert report["manifest"]["reserved1"] == UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION
        assert report["verification"]["signature_valid"] is True

        elf = (
            ROOT
            / "firmware"
            / "exp066_research_platform_core"
            / "build"
            / f"slot_{slot}"
            / f"exp066_research_platform_core_slot_{slot}.elf"
        )
        binary = elf.with_suffix(".bin")
        assert_no_flash_load_reaches_1m(elf)
        assert len(binary.read_bytes()) <= LAYOUT[f"slot_{slot}_payload_max_size"]
        initial_msp, reset_vector = struct.unpack_from("<II", binary.read_bytes(), 0)
        reset_address = reset_vector & ~1
        assert initial_msp == LAYOUT["application_msp_end"]
        assert LAYOUT[f"slot_{slot}_payload_base"] <= reset_address < LAYOUT[f"slot_{slot}_end"]
        assert sha256(elf)
        assert sha256(binary)


def test_exp066_signed_target_builds_bootloader_v2_slot_a_package(
    tmp_path: Path,
) -> None:
    seed = bytes(32)
    seed_path = tmp_path / "test_seed.bin"
    seed_path.write_bytes(seed)
    public_key_hex = bytes(SigningKey(seed).verify_key).hex()

    result = subprocess.run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "clean",
            "verify-signed",
            "LAYOUT_PROFILE=stm32f429_1m",
            f"SIGNING_SEED={seed_path}",
            f"PUBLIC_KEY_HEX={public_key_hex}",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )

    assert result.returncode == 0, result.stdout + result.stderr

    artifact = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "build"
        / "exp066_research_platform_core_slot_a_update_v2.bin"
    )
    legacy_artifact = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "build"
        / "exp066_research_platform_core_signed.bin"
    )
    report_path = (
        ROOT
        / "firmware"
        / "exp066_research_platform_core"
        / "build"
        / "exp066_research_platform_core_slot_a_package_verify.json"
    )

    assert artifact.exists()
    assert not legacy_artifact.exists()

    manifest = decode_signed_manifest(artifact)
    assert manifest["magic"] == UPDATE_PACKAGE_MAGIC
    assert manifest["header_version"] == UPDATE_PACKAGE_FORMAT_VERSION
    assert manifest["image_version"] >= 2
    assert manifest["vector_address"] == LAYOUT["slot_a_payload_base"]
    assert manifest["flags"] == 0
    assert manifest["reserved0"] == UPDATE_PACKAGE_TARGET_STM32F429IGT6_AB_V1
    assert manifest["reserved1"] == UPDATE_PACKAGE_IMAGE_TYPE_APPLICATION
    assert (
        manifest["total_size"]
        == LAYOUT["signed_image_header_size"] + manifest["image_size"]
    )

    data = artifact.read_bytes()
    payload = data[LAYOUT["signed_image_header_size"]:]
    assert hashlib.sha512(payload).digest() == manifest["payload_sha512"]

    report = json.loads(report_path.read_text(encoding="ascii"))
    assert report["result"] == "ok"
    assert report["verification"]["signature_valid"] is True
    assert report["package_sha256"] == sha256(artifact)
    assert report["public_key_sha256"] == hashlib.sha256(
        bytes.fromhex(public_key_hex)
    ).hexdigest()
    assert report["verification_timestamp_utc"]

    negative_build = tmp_path / "negative_make_build"
    negative_project = "exp066_research_platform_core_negative"
    negative_ok = subprocess.run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "clean",
            "verify-update-package",
            f"BUILD={negative_build}",
            f"PROJECT={negative_project}",
            "LAYOUT_PROFILE=stm32f429_1m",
            f"SIGNING_SEED={seed_path}",
            f"PUBLIC_KEY_HEX={public_key_hex}",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    assert negative_ok.returncode == 0, negative_ok.stdout + negative_ok.stderr

    wrong_key_hex = bytes(SigningKey(bytes(reversed(range(32)))).verify_key).hex()
    failed = subprocess.run(
        [
            "make",
            "-C",
            "firmware/exp066_research_platform_core",
            "verify-update-package",
            f"BUILD={negative_build}",
            f"PROJECT={negative_project}",
            "LAYOUT_PROFILE=stm32f429_1m",
            f"SIGNING_SEED={seed_path}",
            f"PUBLIC_KEY_HEX={wrong_key_hex}",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )

    assert failed.returncode != 0
    assert "Verifying update package:" in failed.stdout
    assert "key source : --public-key-hex" in failed.stdout
    failed_report_path = (
        negative_build / f"{negative_project}_slot_a_package_verify.json"
    )
    failed_report = json.loads(failed_report_path.read_text(encoding="ascii"))
    assert failed_report["result"] == "failed"
    failed_package = negative_build / f"{negative_project}_slot_a_update_v2.bin"
    assert failed_report["package_sha256"] == sha256(failed_package)
    assert failed_report["public_key_sha256"] == hashlib.sha256(
        bytes.fromhex(wrong_key_hex)
    ).hexdigest()
    assert "signature verification failed" in failed_report["error"]
