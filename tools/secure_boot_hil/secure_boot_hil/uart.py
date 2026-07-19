"""UART capture and boot-frame extraction."""

from __future__ import annotations

import time
from collections.abc import Callable
from pathlib import Path

from .config import HilConfig
from .errors import UartError
from .model import UartFrame

BANNER_MARKER = "STM32F429 SECURITY LAB"
BOOTLOADER_MARKER = "EXP045 BOOTLOADER V2"
FRAME_PREFIX = "========================================"
TERMINAL_MARKERS = (
    "EXP066 RESEARCH PLATFORM",
    "Application will NOT be started.",
    "Bootloader halted safely.",
    "Jumping to application...",
)


def decode_uart(raw: bytes) -> str:
    return raw.decode("utf-8", errors="replace")


def _frame_start(text: str, marker_index: int) -> int:
    prefix = text.rfind(FRAME_PREFIX, 0, marker_index)
    if prefix == -1:
        return marker_index
    return prefix


def is_complete_frame(text: str) -> bool:
    return (
        BANNER_MARKER in text
        and BOOTLOADER_MARKER in text
        and any(marker in text for marker in TERMINAL_MARKERS)
    )


def extract_frames(raw: bytes) -> list[UartFrame]:
    text = decode_uart(raw)
    marker_positions: list[int] = []
    search_from = 0
    while True:
        index = text.find(BANNER_MARKER, search_from)
        if index < 0:
            break
        marker_positions.append(index)
        search_from = index + len(BANNER_MARKER)

    frames: list[UartFrame] = []
    if not marker_positions:
        return frames

    starts = [_frame_start(text, index) for index in marker_positions]
    for position, start in enumerate(starts):
        end = starts[position + 1] if position + 1 < len(starts) else len(text)
        frame_text = text[start:end]
        frames.append(
            UartFrame(
                text=frame_text,
                complete=is_complete_frame(frame_text),
                start_offset=start,
                end_offset=end,
            )
        )
    return frames


def select_newest_complete_frame(frames: list[UartFrame]) -> UartFrame | None:
    for frame in reversed(frames):
        if frame.complete:
            return frame
    return None


class UartCollector:
    def __init__(self, config: HilConfig) -> None:
        self.config = config

    def capture_after_reset(
        self,
        *,
        raw_path: Path,
        frame_dir: Path,
        reset: Callable[[], object],
    ) -> UartFrame:
        try:
            import serial
        except ImportError as exc:
            raise UartError("pyserial is required for hardware UART capture") from exc

        raw_path.parent.mkdir(parents=True, exist_ok=True)
        frame_dir.mkdir(parents=True, exist_ok=True)

        try:
            with serial.Serial(
                self.config.uart_device.as_posix(),
                self.config.baud,
                timeout=0.05,
            ) as port:
                port.reset_input_buffer()
                reset()
                deadline = time.monotonic() + self.config.capture_seconds
                chunks: list[bytes] = []
                while time.monotonic() < deadline:
                    chunk = port.read(4096)
                    if chunk:
                        chunks.append(chunk)
                raw = b"".join(chunks)
        except OSError as exc:
            raise UartError(f"UART capture failed: {exc}") from exc

        raw_path.write_bytes(raw)
        frames = extract_frames(raw)
        selected = select_newest_complete_frame(frames)
        for index, frame in enumerate(frames):
            frame_path = frame_dir / f"frame_{index:03d}.txt"
            frame_path.write_text(frame.text, encoding="utf-8")
            frames[index] = UartFrame(
                text=frame.text,
                complete=frame.complete,
                start_offset=frame.start_offset,
                end_offset=frame.end_offset,
                path=frame_path,
            )
            if selected is frame:
                selected = frames[index]
        if selected is None:
            raise UartError("UART capture did not contain a complete boot frame")
        return selected
