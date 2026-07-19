"""Structured logging helpers."""

from __future__ import annotations

import json
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


class RunLogger:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def event(self, event: str, **fields: Any) -> None:
        payload = {"timestamp_utc": utc_now(), "event": event, **fields}
        with self.path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(payload, sort_keys=True) + "\n")
