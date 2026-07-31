import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESET_TESTS = ROOT / "tests" / "reset_cause"


def test_reset_cause_policy_host_tests() -> None:
    result = subprocess.run(
        ["make", "-C", str(RESET_TESTS), "clean", "test"],
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
