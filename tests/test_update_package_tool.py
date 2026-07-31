import hashlib
import json
import struct
import subprocess
import sys
from datetime import datetime
from pathlib import Path

from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "update_package.py"

sys.path.insert(0, str(ROOT / "tools"))
from stm32f429_layout import LAYOUT  # noqa: E402

SEED = bytes(range(32))
PUBLIC_KEY_HEX = bytes(SigningKey(SEED).verify_key).hex()


def make_application(slot: str, size: int = 128) -> bytes:
    image = bytearray(b"\xA5" * size)
    vector_base = LAYOUT[f"slot_{slot}_payload_base"]
    struct.pack_into(
        "<II",
        image,
        0,
        LAYOUT["application_msp_end"],
        vector_base | 1,
    )
    return bytes(image)


def run_tool(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        text=True,
        capture_output=True,
        check=False,
    )


def write_public_key_header(path: Path, public_key: bytes) -> None:
    values = ", ".join(f"0x{byte:02X}U" for byte in public_key)
    path.write_text(
        "#ifndef TEST_FIRMWARE_PUBLIC_KEY_H\n"
        "#define TEST_FIRMWARE_PUBLIC_KEY_H\n"
        "#include <stdint.h>\n"
        f"static const uint8_t firmware_public_key[32] = {{ {values} }};\n"
        "#endif\n",
        encoding="ascii",
    )


def build_package(
    tmp_path: Path,
    *,
    slot: str,
    seed: bytes = SEED,
    image_version: int = 3,
) -> tuple[Path, Path, bytes]:
    seed_path = tmp_path / f"seed_{slot}.bin"
    app = tmp_path / f"slot_{slot}_app.bin"
    package = tmp_path / f"slot_{slot}.update.bin"

    seed_path.write_bytes(seed)
    app.write_bytes(make_application(slot))

    result = run_tool(
        "build",
        "--application",
        str(app),
        "--seed",
        str(seed_path),
        "--slot",
        slot,
        "--image-version",
        str(image_version),
        "--output",
        str(package),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return app, package, bytes(SigningKey(seed).verify_key)


def verify_package(
    package: Path,
    *,
    slot: str,
    report: Path,
    application: Path | None = None,
    public_key_hex: str | None = None,
    public_key_header: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    args = [
        "verify",
        "--package",
        str(package),
        "--slot",
        slot,
        "--json-output",
        str(report),
    ]
    if application is not None:
        args.extend(["--application", str(application)])
    if public_key_hex is not None:
        args.extend(["--public-key-hex", public_key_hex])
    if public_key_header is not None:
        args.extend(["--public-key-header", str(public_key_header)])
    return run_tool(*args)


def assert_common_verify_report(
    report: dict[str, object],
    *,
    package: Path,
    public_key: bytes,
    slot: str,
    application: Path | None,
) -> None:
    package_bytes = package.read_bytes()
    assert report["result"] == "ok"
    assert report["package_sha256"] == hashlib.sha256(package_bytes).hexdigest()
    assert report["package_size"] == len(package_bytes)
    assert report["public_key_hex"] == public_key.hex()
    assert report["public_key_sha256"] == hashlib.sha256(public_key).hexdigest()
    assert report["slot"] == slot
    assert report["image_version"] == 3
    assert report["signature_valid"] is True
    assert report["payload_hash_valid"] is True
    assert report["application_payload_match"] is (application is not None)
    assert report["signed_region_offset"] == 0
    assert report["signed_region_size"] == LAYOUT["signed_manifest_size"]
    assert "serialized manifest" in str(report["signed_region_description"])
    assert report["layout_profile"] == "stm32f429_1m"
    assert report["target"] == "STM32F429IGT6"
    assert str(report["tool_path"]).endswith("tools/update_package.py")
    git_commit = report["git_commit"]
    assert git_commit is None or (
        isinstance(git_commit, str)
        and len(git_commit) == 40
        and all(c in "0123456789abcdef" for c in git_commit.lower())
    )
    assert isinstance(report["verification_timestamp_utc"], str)
    datetime.fromisoformat(
        str(report["verification_timestamp_utc"]).replace("Z", "+00:00")
    )
    if application is not None:
        assert report["application_sha256"] == hashlib.sha256(
            application.read_bytes()
        ).hexdigest()


def test_build_inspect_verify_and_install_sim(tmp_path: Path) -> None:
    seed = tmp_path / "seed.bin"
    app = tmp_path / "slot_b_app.bin"
    package = tmp_path / "update.supkg"
    flash = tmp_path / "flash.bin"
    build_report = tmp_path / "build.json"
    inspect_report = tmp_path / "inspect.json"
    verify_report = tmp_path / "verify.json"
    install_report = tmp_path / "install.json"

    seed.write_bytes(SEED)
    app.write_bytes(make_application("b"))

    result = run_tool(
        "build",
        "--application",
        str(app),
        "--seed",
        str(seed),
        "--slot",
        "b",
        "--image-version",
        "3",
        "--output",
        str(package),
        "--json-output",
        str(build_report),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    built = json.loads(build_report.read_text(encoding="ascii"))
    assert built["result"] == "ok"
    assert built["target_layout"]["mcu"] == "STM32F429IGT6"
    assert built["target_layout"]["layout_profile"] == "stm32f429_1m"
    assert built["target_layout"]["flash_size"] == 0x00100000
    assert built["target_layout"]["slots"]["b"]["payload_base"] == 0x08080200
    assert built["public_key_fingerprint_sha256"] == hashlib.sha256(
        bytes.fromhex(PUBLIC_KEY_HEX)
    ).hexdigest()
    assert built["signature_self_verification"] is True

    result = run_tool(
        "inspect",
        "--package",
        str(package),
        "--json-output",
        str(inspect_report),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    inspected = json.loads(inspect_report.read_text(encoding="ascii"))
    assert inspected["target_layout"]["sector_count"] == 12
    assert inspected["package"]["slot"] == "b"
    assert inspected["package"]["format_version"] == 2
    assert inspected["package"]["image_version"] == 3

    result = run_tool(
        "verify",
        "--package",
        str(package),
        "--slot",
        "b",
        "--application",
        str(app),
        "--public-key-hex",
        PUBLIC_KEY_HEX,
        "--json-output",
        str(verify_report),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    verified = json.loads(verify_report.read_text(encoding="ascii"))
    assert verified["result"] == "ok"
    assert verified["target_layout"]["layout_profile"] == "stm32f429_1m"
    assert verified["verification"]["signature_valid"] is True

    result = run_tool(
        "install-sim",
        "--package",
        str(package),
        "--active-slot",
        "a",
        "--active-version",
        "2",
        "--public-key-hex",
        PUBLIC_KEY_HEX,
        "--flash-output",
        str(flash),
        "--json-output",
        str(install_report),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    installed = json.loads(install_report.read_text(encoding="ascii"))
    assert installed["result"] == "ok"
    assert installed["target_layout"]["flash_size"] == 0x00100000
    assert installed["inactive_slot"] == "b"
    assert installed["metadata_states"][-1] == "CANDIDATE_READY"
    assert flash.stat().st_size == LAYOUT["flash_total_size"]


def test_update_package_verify_reports_are_reproducible_for_slots_and_key_sources(
    tmp_path: Path,
) -> None:
    for slot in ("a", "b"):
        app, package, public_key = build_package(tmp_path, slot=slot)
        header = tmp_path / f"public_key_{slot}.h"
        write_public_key_header(header, public_key)

        hex_report_path = tmp_path / f"verify_{slot}_hex.json"
        result = verify_package(
            package,
            slot=slot,
            application=app,
            public_key_hex=public_key.hex(),
            report=hex_report_path,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        hex_report = json.loads(hex_report_path.read_text(encoding="ascii"))
        assert_common_verify_report(
            hex_report,
            package=package,
            public_key=public_key,
            slot=slot,
            application=app,
        )
        assert hex_report["public_key_source"] == "public-key-hex"

        header_report_path = tmp_path / f"verify_{slot}_header.json"
        result = verify_package(
            package,
            slot=slot,
            application=app,
            public_key_header=header,
            report=header_report_path,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        header_report = json.loads(header_report_path.read_text(encoding="ascii"))
        assert_common_verify_report(
            header_report,
            package=package,
            public_key=public_key,
            slot=slot,
            application=app,
        )
        assert header_report["public_key_source"] == "public-key-header"
        assert header_report["public_key_hex"] == hex_report["public_key_hex"]
        assert header_report["public_key_sha256"] == hex_report["public_key_sha256"]

        no_app_report_path = tmp_path / f"verify_{slot}_no_app.json"
        result = verify_package(
            package,
            slot=slot,
            public_key_header=header,
            report=no_app_report_path,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        no_app_report = json.loads(no_app_report_path.read_text(encoding="ascii"))
        assert_common_verify_report(
            no_app_report,
            package=package,
            public_key=public_key,
            slot=slot,
            application=None,
        )


def test_update_package_verify_rejects_tampering_and_reports_fingerprints(
    tmp_path: Path,
) -> None:
    app, package, public_key = build_package(tmp_path, slot="a")
    wrong_key = bytes(SigningKey(bytes(reversed(range(32)))).verify_key)

    cases = []
    for label, offset in (
        ("payload", -1),
        ("manifest", 32),
        ("signature", LAYOUT["signed_manifest_size"]),
    ):
        tampered = tmp_path / f"{label}.update.bin"
        data = bytearray(package.read_bytes())
        data[offset] ^= 0x01
        tampered.write_bytes(data)
        cases.append((label, tampered, public_key.hex()))

    cases.append(("wrong_key", package, wrong_key.hex()))

    for label, path, key_hex in cases:
        report_path = tmp_path / f"{label}.json"
        result = verify_package(
            path,
            slot="a",
            public_key_hex=key_hex,
            report=report_path,
        )
        assert result.returncode == 1
        report = json.loads(report_path.read_text(encoding="ascii"))
        assert report["result"] == "failed"
        assert report["package_sha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
        assert report["public_key_sha256"] == hashlib.sha256(
            bytes.fromhex(key_hex)
        ).hexdigest()
        assert report["signed_region_size"] == LAYOUT["signed_manifest_size"]


def test_verify_report_matches_rebuilt_package_at_same_path(tmp_path: Path) -> None:
    app, package, public_key = build_package(tmp_path, slot="a", image_version=3)
    report_path = tmp_path / "verify.json"

    result = verify_package(
        package,
        slot="a",
        application=app,
        public_key_hex=public_key.hex(),
        report=report_path,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    first = json.loads(report_path.read_text(encoding="ascii"))

    app, package, public_key = build_package(tmp_path, slot="a", image_version=4)
    result = verify_package(
        package,
        slot="a",
        application=app,
        public_key_hex=public_key.hex(),
        report=report_path,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    second = json.loads(report_path.read_text(encoding="ascii"))

    assert first["package_sha256"] != second["package_sha256"]
    assert second["package_sha256"] == hashlib.sha256(package.read_bytes()).hexdigest()
    assert second["image_version"] == 4


def test_verify_rejects_wrong_key_and_trailing_data(tmp_path: Path) -> None:
    seed = tmp_path / "seed.bin"
    app = tmp_path / "slot_b_app.bin"
    package = tmp_path / "update.supkg"
    report = tmp_path / "verify.json"

    seed.write_bytes(SEED)
    app.write_bytes(make_application("b"))
    assert run_tool(
        "build",
        "--application",
        str(app),
        "--seed",
        str(seed),
        "--slot",
        "b",
        "--image-version",
        "3",
        "--output",
        str(package),
    ).returncode == 0

    wrong_key = bytes(SigningKey(bytes(reversed(range(32)))).verify_key).hex()
    result = run_tool(
        "verify",
        "--package",
        str(package),
        "--slot",
        "b",
        "--public-key-hex",
        wrong_key,
        "--json-output",
        str(report),
    )
    assert result.returncode == 1
    assert "signature verification failed" in report.read_text(encoding="ascii")

    package.write_bytes(package.read_bytes() + b"\xFF")
    result = run_tool(
        "inspect",
        "--package",
        str(package),
        "--json-output",
        str(report),
    )
    assert result.returncode == 1
    assert "trailing data" in report.read_text(encoding="ascii")
