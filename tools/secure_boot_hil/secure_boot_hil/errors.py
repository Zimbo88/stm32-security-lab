"""Framework-specific exception hierarchy."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .model import CommandResult


class HilError(Exception):
    """Base class for recoverable HIL framework errors."""


class ConfigError(HilError):
    """Configuration is malformed or unsafe."""


class EnvironmentError(HilError):
    """Required host or repository prerequisite is missing."""


class ProcessError(HilError):
    """A subprocess failed or timed out."""

    def __init__(self, message: str, *, result: CommandResult | None = None) -> None:
        super().__init__(message)
        self.result = result


class FlashError(HilError):
    """Flash backup, programming, reset, or readback failed."""


class RestoreError(HilError):
    """Restoration or restore verification failed."""


class UartError(HilError):
    """UART capture failed."""


class ReportError(HilError):
    """A required report could not be written."""
