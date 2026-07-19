import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path

from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[1]
RELEASE_TOOL = ROOT / "tools" / "release_artifacts.py"
SIGNER_PATH = (
    ROOT
    / "firmware"
    / "exp065_signed_app"
    / "tools"
    / "build_signed_image.py"
)
DETERMINISTIC_PATH = ROOT / "tools" / "check_deterministic_build.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


signer = load_module("build_signed_image_for_release_tests", SIGNER_PATH)
deterministic = load_module("check_deterministic_build_for_tests", DETERMINISTIC_PATH)

SEED = bytes(range(32))
PUBLIC_KEY_HEX = bytes(SigningKey(SEED).verify_key).hex()


def make_application(size: int = 128) -> bytes:
    image = bytearray(b"\xA5" * size)
    struct.pack_into(
        "<II",
        image,
        0,
        signer.APPLICATION_MSP_END,
        signer.APPLICATION_BASE | 1,
    )
    return bytes(image)


def sign_application(application: bytes) -> bytes:
    signed, _, _, _ = signer.build_signed_image(application, SEED)
    return signed


def sign_application_with_hash(application: bytes, payload_hash: bytes) -> bytes:
    manifest = signer.MANIFEST_STRUCT.pack(
        signer.SIGNED_IMAGE_MAGIC,
        signer.SIGNED_HEADER_VERSION,
        signer.IMAGE_VERSION,
        signer.APPLICATION_BASE,
        len(application),
        0,
        0,
        0,
        payload_hash,
    )
    signature = SigningKey(SEED).sign(manifest).signature
    padding = (
        b"\xFF"
        * (signer.APPLICATION_OFFSET - signer.MANIFEST_SIZE - signer.SIGNATURE_SIZE)
    )
    return manifest + signature + padding + application


def run_tool(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(RELEASE_TOOL), *args],
        text=True,
        capture_output=True,
        check=False,
    )


def write_release_artifacts(tmp_path: Path, application: bytes, signed_image: bytes) -> dict[str, Path]:
    project = tmp_path / "firmware" / "exp066_research_platform_core"
    build = project / "build"
    build.mkdir(parents=True)
    (project / "Makefile").write_text(
        "CC := arm-none-eabi-gcc\n"
        "CPUFLAGS := -mcpu=cortex-m4 -mthumb\n"
        "CFLAGS := $(CPUFLAGS) -Wall -Wextra -Werror\n"
        "LDFLAGS := $(CPUFLAGS) -nostdlib\n",
        encoding="ascii",
    )

    paths = {
        "bootloader_elf": tmp_path / "bootloader.elf",
        "bootloader_bin": tmp_path / "bootloader.bin",
        "bootloader_hex": tmp_path / "bootloader.hex",
        "application_elf": build / "exp066_research_platform_core.elf",
        "application_bin": build / "exp066_research_platform_core.bin",
        "application_hex": build / "exp066_research_platform_core.hex",
        "signed_image": build / "exp066_research_platform_core_signed.bin",
        "manifest": build / "release_manifest.json",
        "report": build / "release_report.json",
    }
    paths["bootloader_elf"].write_bytes(b"\x7fELFbootloader")
    paths["bootloader_bin"].write_bytes(b"bootloader")
    paths["bootloader_hex"].write_text(":00000001FF\n", encoding="ascii")
    paths["application_elf"].write_bytes(b"\x7fELFapplication")
    paths["application_bin"].write_bytes(application)
    paths["application_hex"].write_text(":00000001FF\n", encoding="ascii")
    paths["signed_image"].write_bytes(signed_image)
    return paths


def release_args(paths: dict[str, Path], *extra: str) -> list[str]:
    return [
        "verify-release",
        "--bootloader-elf",
        str(paths["bootloader_elf"]),
        "--bootloader-bin",
        str(paths["bootloader_bin"]),
        "--bootloader-hex",
        str(paths["bootloader_hex"]),
        "--application-elf",
        str(paths["application_elf"]),
        "--application-bin",
        str(paths["application_bin"]),
        "--application-hex",
        str(paths["application_hex"]),
        "--signed-image",
        str(paths["signed_image"]),
        "--public-key-hex",
        PUBLIC_KEY_HEX,
        "--manifest-output",
        str(paths["manifest"]),
        "--report-output",
        str(paths["report"]),
        "--compiler",
        sys.executable,
        "--git-commit",
        "0123456789abcdef",
        *extra,
    ]


def test_verify_signed_image_outputs_json(tmp_path: Path) -> None:
    application = make_application()
    signed_image = tmp_path / "signed.bin"
    application_path = tmp_path / "app.bin"
    report = tmp_path / "verify.json"
    signed_image.write_bytes(sign_application(application))
    application_path.write_bytes(application)

    result = run_tool(
        "verify-signed",
        "--signed-image",
        str(signed_image),
        "--application",
        str(application_path),
        "--public-key-hex",
        PUBLIC_KEY_HEX,
        "--json-output",
        str(report),
    )

    assert result.returncode == 0, result.stdout + result.stderr
    data = json.loads(report.read_text(encoding="ascii"))
    assert data["result"] == "ok"
    assert data["target_layout"]["mcu"] == "STM32F429IGT6"
    assert data["target_layout"]["layout_profile"] == "stm32f429_1m"
    assert data["target_layout"]["flash_size"] == 0x00100000
    assert data["manifest"]["image_version"] == signer.IMAGE_VERSION
    assert data["verification"]["signature_valid"] is True


def test_verify_release_outputs_target_layout(tmp_path: Path) -> None:
    application = make_application()
    paths = write_release_artifacts(tmp_path, application, sign_application(application))

    result = run_tool(*release_args(paths))

    assert result.returncode == 0, result.stdout + result.stderr
    manifest = json.loads(paths["manifest"].read_text(encoding="ascii"))
    assert manifest["target_layout"]["mcu"] == "STM32F429IGT6"
    assert manifest["target_layout"]["layout_profile"] == "stm32f429_1m"
    assert manifest["target_layout"]["flash_size"] == 0x00100000
    assert manifest["target_layout"]["sector_count"] == 12
    assert manifest["target_layout"]["slots"]["a"]["payload_base"] == 0x08020200
    assert manifest["target_layout"]["slots"]["b"]["payload_base"] == 0x08080200


def test_verify_signed_rejects_modified_payload(tmp_path: Path) -> None:
    application = make_application()
    signed = bytearray(sign_application(application))
    signed[-1] ^= 0x01
    signed_path = tmp_path / "signed.bin"
    app_path = tmp_path / "app.bin"
    signed_path.write_bytes(signed)
    app_path.write_bytes(application)

    result = run_tool(
        "verify-signed",
        "--signed-image",
        str(signed_path),
        "--application",
        str(app_path),
        "--public-key-hex",
        PUBLIC_KEY_HEX,
    )

    assert result.returncode == 1
    assert "does not match application binary" in result.stdout


def test_verify_signed_rejects_wrong_signature(tmp_path: Path) -> None:
    signed = bytearray(sign_application(make_application()))
    signed[signer.MANIFEST_SIZE] ^= 0x01
    signed_path = tmp_path / "signed.bin"
    signed_path.write_bytes(signed)

    result = run_tool(
        "verify-signed",
        "--signed-image",
        str(signed_path),
        "--public-key-hex",
        PUBLIC_KEY_HEX,
    )

    assert result.returncode == 1
    assert "signature verification failed" in result.stdout


def test_verify_signed_rejects_wrong_manifest_version(tmp_path: Path) -> None:
    signed = bytearray(sign_application(make_application()))
    struct.pack_into("<I", signed, 4, signer.SIGNED_HEADER_VERSION + 1)
    signed_path = tmp_path / "signed.bin"
    signed_path.write_bytes(signed)

    result = run_tool(
        "verify-signed",
        "--signed-image",
        str(signed_path),
        "--public-key-hex",
        PUBLIC_KEY_HEX,
    )

    assert result.returncode == 1
    assert "unsupported manifest header version" in result.stdout


def test_verify_signed_rejects_wrong_payload_hash(tmp_path: Path) -> None:
    signed_path = tmp_path / "signed.bin"
    signed_path.write_bytes(sign_application_with_hash(make_application(), b"\x00" * 64))

    result = run_tool(
        "verify-signed",
        "--signed-image",
        str(signed_path),
        "--public-key-hex",
        PUBLIC_KEY_HEX,
    )

    assert result.returncode == 1
    assert "payload SHA-512 does not match manifest" in result.stdout


def test_verify_release_rejects_missing_artifact(tmp_path: Path) -> None:
    application = make_application()
    paths = write_release_artifacts(tmp_path, application, sign_application(application))
    paths["application_hex"].unlink()

    result = run_tool(*release_args(paths))

    assert result.returncode == 1
    assert "failed to read HEX" in paths["report"].read_text(encoding="ascii")


def test_verify_release_rejects_version_mismatch(tmp_path: Path) -> None:
    application = make_application()
    paths = write_release_artifacts(tmp_path, application, sign_application(application))

    result = run_tool(*release_args(paths, "--expected-application-version", "3"))

    assert result.returncode == 1
    assert "application version 2 != expected 3" in paths["report"].read_text(encoding="ascii")


def test_verify_release_rejects_application_mismatch(tmp_path: Path) -> None:
    application = make_application()
    changed = bytearray(application)
    changed[-1] ^= 0x01
    paths = write_release_artifacts(tmp_path, bytes(changed), sign_application(application))

    result = run_tool(*release_args(paths))

    assert result.returncode == 1
    assert "payload does not match application binary" in paths["report"].read_text(encoding="ascii")


def test_deterministic_mismatch_reports_changed_output() -> None:
    left = {name: "same" for name in deterministic.BUILD_OUTPUTS}
    right = dict(left)
    right[deterministic.BUILD_OUTPUTS[0]] = "different"

    assert deterministic.mismatched_outputs(left, right) == [deterministic.BUILD_OUTPUTS[0]]
    assert any(name.endswith(".elf") for name in deterministic.BUILD_OUTPUTS)
    assert any(name.endswith(".hex") for name in deterministic.BUILD_OUTPUTS)
    assert any(name.endswith("release_manifest.json") for name in deterministic.BUILD_OUTPUTS)
