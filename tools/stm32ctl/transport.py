from __future__ import annotations

from types import TracebackType
from typing import Self

from .errors import TransportError
from .protocol import ByteTransport


class SerialPort:
    def __init__(self, port: str, baudrate: int, timeout: float) -> None:
        try:
            import serial
        except ImportError as exc:
            raise TransportError("pyserial is required; install pyserial>=3.5") from exc

        try:
            self._serial = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)
        except Exception as exc:
            raise TransportError(f"failed to open serial port {port}: {exc}") from exc

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        self.close()

    def write(self, data: bytes) -> int | None:
        try:
            written = self._serial.write(data)
            self._serial.flush()
        except Exception as exc:
            raise TransportError(f"serial write failed: {exc}") from exc
        return int(written)

    def read(self, size: int = 1) -> bytes:
        try:
            return bytes(self._serial.read(size))
        except Exception as exc:
            raise TransportError(f"serial read failed: {exc}") from exc

    def reset_input_buffer(self) -> None:
        try:
            self._serial.reset_input_buffer()
        except Exception as exc:
            raise TransportError(f"failed to flush serial input: {exc}") from exc

    def close(self) -> None:
        self._serial.close()


def open_serial(port: str, baudrate: int, timeout: float) -> ByteTransport:
    return SerialPort(port, baudrate, timeout)
