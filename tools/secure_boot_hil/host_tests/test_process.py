from __future__ import annotations

import sys
from pathlib import Path

import pytest

from secure_boot_hil.errors import ProcessError
from secure_boot_hil.process import ProcessRunner


def test_subprocess_failure_can_be_recorded(tmp_path: Path) -> None:
    result = ProcessRunner().run(
        [sys.executable, "-c", "import sys; sys.exit(3)"],
        cwd=tmp_path,
        check=False,
    )
    assert result.returncode == 3


def test_subprocess_failure_raises_when_checked(tmp_path: Path) -> None:
    with pytest.raises(ProcessError):
        ProcessRunner().run(
            [sys.executable, "-c", "import sys; sys.exit(3)"],
            cwd=tmp_path,
            check=True,
        )


def test_timeout_raises_process_error(tmp_path: Path) -> None:
    with pytest.raises(ProcessError, match="timed out"):
        ProcessRunner().run(
            [sys.executable, "-c", "import time; time.sleep(1)"],
            cwd=tmp_path,
            timeout_seconds=0.01,
        )
