"""st-flash integration with declared-region safety checks."""

from __future__ import annotations

import time
from pathlib import Path

from .config import HilConfig
from .errors import FlashError, ProcessError
from .model import CommandResult, FlashRegion
from .process import ProcessRunner


def assert_region_within_flash(region: FlashRegion, *, flash_base: int, flash_end: int) -> None:
    if region.address < flash_base or region.end > flash_end:
        raise FlashError(f"region {region.name} is outside internal flash")


class StFlash:
    def __init__(self, config: HilConfig, runner: ProcessRunner) -> None:
        self.config = config
        self.runner = runner

    def _run(
        self,
        command: list[str],
        *,
        timeout_seconds: float | None = None,
    ) -> CommandResult:
        attempts = 3
        retry_markers = (
            "LIBUSB_ERROR_TIMEOUT",
            "Failed to enter SWD mode",
            "Failed to connect to target",
            "GET_VERSION send request failed",
            "ENTER_SWD send request failed",
        )

        last_result: CommandResult | None = None

        for attempt in range(1, attempts + 1):
            result = self.runner.run(
                command,
                cwd=self.config.repo_root,
                timeout_seconds=(
                    timeout_seconds or self.config.command_timeout_seconds
                ),
                check=False,
            )
            last_result = result

            if result.returncode == 0:
                return result

            diagnostic_text = "\n".join(
                part
                for part in (result.stdout.strip(), result.stderr.strip())
                if part
            )

            transient_failure = any(
                marker in diagnostic_text
                for marker in retry_markers
            )

            if not transient_failure or attempt == attempts:
                break

            time.sleep(float(attempt))

        assert last_result is not None

        details = []

        if last_result.stdout.strip():
            details.append(
                "stdout:\n" + last_result.stdout.strip()
            )

        if last_result.stderr.strip():
            details.append(
                "stderr:\n" + last_result.stderr.strip()
            )

        message = (
            f"st-flash command failed ({last_result.returncode}) "
            f"after {attempts} attempt(s): "
            f"{' '.join(command)}"
        )

        if details:
            message += "\n\n" + "\n\n".join(details)

        raise FlashError(message)

    def read_region(self, region: FlashRegion, destination: Path) -> CommandResult:
        assert_region_within_flash(
            region,
            flash_base=self.config.flash_base,
            flash_end=self.config.flash_end,
        )
        destination.parent.mkdir(parents=True, exist_ok=True)
        result = self._run(
            [
                self.config.st_flash,
                "read",
                destination.as_posix(),
                f"0x{region.address:08X}",
                str(region.size),
            ]
        )
        if not destination.exists():
            raise FlashError(f"st-flash read did not create {destination}")
        actual_size = destination.stat().st_size
        if actual_size != region.size:
            raise FlashError(
                f"backup for {region.name} has {actual_size} bytes, expected {region.size}"
            )
        return result

    def write_region(self, region: FlashRegion, source: Path) -> CommandResult:
        assert_region_within_flash(
            region,
            flash_base=self.config.flash_base,
            flash_end=self.config.flash_end,
        )
        if not source.exists():
            raise FlashError(f"flash image missing: {source}")
        size = source.stat().st_size
        if size <= 0:
            raise FlashError(f"refusing to flash empty image: {source}")
        if size > region.size:
            raise FlashError(
                f"image {source} has {size} bytes and exceeds region {region.name} "
                f"capacity {region.size}"
            )
        return self._run(
            [
                self.config.st_flash,
                "write",
                source.as_posix(),
                f"0x{region.address:08X}",
            ]
        )

    def reset(self) -> CommandResult:
        return self._run([self.config.st_flash, "reset"], timeout_seconds=30.0)


class FakeFlash:
    """In-memory flash adapter used by host tests."""

    def __init__(self, config: HilConfig) -> None:
        self.config = config
        self.memory = bytearray(b"\xff" * (config.flash_end - config.flash_base))
        self.commands: list[list[str]] = []

    def _offset(self, region: FlashRegion) -> int:
        assert_region_within_flash(
            region,
            flash_base=self.config.flash_base,
            flash_end=self.config.flash_end,
        )
        return region.address - self.config.flash_base

    def read_region(self, region: FlashRegion, destination: Path) -> CommandResult:
        destination.parent.mkdir(parents=True, exist_ok=True)
        offset = self._offset(region)
        destination.write_bytes(bytes(self.memory[offset : offset + region.size]))
        self.commands.append(["read", region.name, destination.as_posix()])
        return CommandResult(
            command=self.commands[-1],
            cwd=self.config.repo_root.as_posix(),
            start_utc="1970-01-01T00:00:00Z",
            end_utc="1970-01-01T00:00:00Z",
            duration_seconds=0.0,
            returncode=0,
            stdout="",
            stderr="",
        )

    def write_region(self, region: FlashRegion, source: Path) -> CommandResult:
        data = source.read_bytes()
        if len(data) > region.size:
            raise FlashError("image exceeds region capacity")
        offset = self._offset(region)
        self.memory[offset : offset + len(data)] = data
        self.commands.append(["write", region.name, source.as_posix()])
        return CommandResult(
            command=self.commands[-1],
            cwd=self.config.repo_root.as_posix(),
            start_utc="1970-01-01T00:00:00Z",
            end_utc="1970-01-01T00:00:00Z",
            duration_seconds=0.0,
            returncode=0,
            stdout="",
            stderr="",
        )

    def reset(self) -> CommandResult:
        self.commands.append(["reset"])
        return CommandResult(
            command=self.commands[-1],
            cwd=self.config.repo_root.as_posix(),
            start_utc="1970-01-01T00:00:00Z",
            end_utc="1970-01-01T00:00:00Z",
            duration_seconds=0.0,
            returncode=0,
            stdout="",
            stderr="",
        )


FlashAdapter = StFlash | FakeFlash


def flash_error_text(error: Exception) -> str:
    if isinstance(error, ProcessError):
        return error.args[0] if error.args else "process failure"
    return str(error)
