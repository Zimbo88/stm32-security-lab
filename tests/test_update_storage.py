import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
UPDATE_STORAGE = ROOT / "tests" / "update_storage"


def run_make(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["make", "-C", str(UPDATE_STORAGE), *args],
        text=True,
        capture_output=True,
        check=False,
    )


def assert_success(result: subprocess.CompletedProcess[str]) -> None:
    assert result.returncode == 0, result.stdout + result.stderr


def test_update_storage_installer_and_stage0_selection() -> None:
    assert_success(run_make("clean", "test"))


def test_update_storage_installer_and_stage0_selection_with_sanitizers_when_supported() -> None:
    result = run_make("clean", "test", "SANITIZE=1")
    output = result.stdout + result.stderr

    if result.returncode != 0 and (
        "unrecognized" in output
        or "unsupported" in output
        or "invalid argument" in output
    ):
        pytest.skip("host compiler does not support requested sanitizers")

    assert_success(result)
