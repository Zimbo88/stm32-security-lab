import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
HOST_VERIFIER = ROOT / "tests" / "host_verifier"


def run_make(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["make", "-C", str(HOST_VERIFIER), *args],
        text=True,
        capture_output=True,
        check=False,
    )


def assert_success(result: subprocess.CompletedProcess[str]) -> None:
    assert result.returncode == 0, result.stdout + result.stderr


def test_host_signed_image_verifier() -> None:
    assert_success(run_make("clean", "test"))


def test_host_signed_image_verifier_with_sanitizers_when_supported() -> None:
    result = run_make("clean", "test", "SANITIZE=1")
    output = result.stdout + result.stderr

    if result.returncode != 0 and (
        "unrecognized" in output
        or "unsupported" in output
        or "invalid argument" in output
    ):
        pytest.skip("host compiler does not support requested sanitizers")

    assert_success(result)
