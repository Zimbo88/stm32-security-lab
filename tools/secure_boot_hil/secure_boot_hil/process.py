"""Subprocess execution with deterministic records."""

from __future__ import annotations

import subprocess
import time
from pathlib import Path

from .errors import ProcessError
from .logging import utc_now
from .model import CommandResult


class ProcessRunner:
    def run(
        self,
        command: list[str],
        *,
        cwd: Path,
        timeout_seconds: float | None = None,
        check: bool = True,
    ) -> CommandResult:
        start = time.monotonic()
        start_utc = utc_now()
        try:
            completed = subprocess.run(
                command,
                cwd=cwd,
                text=True,
                capture_output=True,
                timeout=timeout_seconds,
                check=False,
            )
            duration = time.monotonic() - start
            result = CommandResult(
                command=command,
                cwd=cwd.as_posix(),
                start_utc=start_utc,
                end_utc=utc_now(),
                duration_seconds=duration,
                returncode=completed.returncode,
                stdout=completed.stdout,
                stderr=completed.stderr,
            )
        except subprocess.TimeoutExpired as exc:
            duration = time.monotonic() - start
            result = CommandResult(
                command=command,
                cwd=cwd.as_posix(),
                start_utc=start_utc,
                end_utc=utc_now(),
                duration_seconds=duration,
                returncode=-1,
                stdout=exc.stdout if isinstance(exc.stdout, str) else "",
                stderr=exc.stderr if isinstance(exc.stderr, str) else "",
                timed_out=True,
            )
            raise ProcessError(f"command timed out: {' '.join(command)}", result=result) from exc
        except OSError as exc:
            raise ProcessError(f"failed to execute {' '.join(command)}: {exc}") from exc

        if check and result.returncode != 0:
            raise ProcessError(
                f"command failed with {result.returncode}: {' '.join(command)}",
                result=result,
            )
        return result


class RecordingProcessRunner(ProcessRunner):
    def __init__(self) -> None:
        self.commands: list[CommandResult] = []

    def run(
        self,
        command: list[str],
        *,
        cwd: Path,
        timeout_seconds: float | None = None,
        check: bool = True,
    ) -> CommandResult:
        result = super().run(
            command,
            cwd=cwd,
            timeout_seconds=timeout_seconds,
            check=check,
        )
        self.commands.append(result)
        return result
