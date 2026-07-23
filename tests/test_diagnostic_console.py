import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
DIAGNOSTIC_CONSOLE_TESTS = ROOT / "tests" / "diagnostic_console"


def run_make(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["make", "-C", str(DIAGNOSTIC_CONSOLE_TESTS), *args],
        text=True,
        capture_output=True,
        check=False,
    )


def assert_success(result: subprocess.CompletedProcess[str]) -> None:
    assert result.returncode == 0, result.stdout + result.stderr


def test_diagnostic_console_host_tests() -> None:
    assert_success(run_make("clean", "test"))


def test_diagnostic_console_host_tests_with_sanitizers_when_supported() -> None:
    result = run_make("clean", "test", "SANITIZE=1")
    output = result.stdout + result.stderr

    if result.returncode != 0 and (
        "unrecognized" in output
        or "unsupported" in output
        or "invalid argument" in output
    ):
        pytest.skip("host compiler does not support requested sanitizers")

    assert_success(result)
