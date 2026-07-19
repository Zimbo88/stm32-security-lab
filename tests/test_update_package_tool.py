import json
import struct
import subprocess
import sys
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
    assert json.loads(build_report.read_text(encoding="ascii"))["result"] == "ok"

    result = run_tool(
        "inspect",
        "--package",
        str(package),
        "--json-output",
        str(inspect_report),
    )
    assert result.returncode == 0, result.stdout + result.stderr
    inspected = json.loads(inspect_report.read_text(encoding="ascii"))
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
    assert installed["inactive_slot"] == "b"
    assert installed["metadata_states"][-1] == "CANDIDATE_READY"
    assert flash.stat().st_size == LAYOUT["flash_total_size"]


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
