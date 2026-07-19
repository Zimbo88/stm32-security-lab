"""Idempotent flash restoration transaction."""

from __future__ import annotations

import json
from pathlib import Path
from types import TracebackType
from typing import Literal

from .backup import BackupManager
from .build import sha256_file
from .config import HilConfig
from .errors import RestoreError
from .flash import FlashAdapter
from .logging import utc_now
from .model import BackupRecord, RestoreRecord


class RestoreTransaction:
    def __init__(
        self,
        config: HilConfig,
        flash: FlashAdapter,
        run_dir: Path,
        *,
        enabled: bool = True,
        keep_test_state: bool = False,
    ) -> None:
        self.config = config
        self.flash = flash
        self.run_dir = run_dir
        self.enabled = enabled
        self.keep_test_state = keep_test_state
        self.backups: list[BackupRecord] = []
        self.restore_records: list[RestoreRecord] = []
        self.restored = False

    def __enter__(self) -> RestoreTransaction:
        if self.enabled:
            self.backups = BackupManager(self.config, self.flash).backup_all(self.run_dir)
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> Literal[False]:
        if self.enabled and not self.keep_test_state:
            self.restore()
        return False

    def restore(self) -> list[RestoreRecord]:
        if not self.backups:
            raise RestoreError("cannot restore before backups have been captured")

        readback_dir = self.run_dir / "restore-readback"
        readback_dir.mkdir(parents=True, exist_ok=True)
        records: list[RestoreRecord] = []
        for backup in self.backups:
            self.flash.write_region(backup.region, backup.path)
            readback_path = readback_dir / f"{backup.region.name}.bin"
            self.flash.read_region(backup.region, readback_path)
            readback_sha256 = sha256_file(readback_path)
            records.append(
                RestoreRecord(
                    region=backup.region,
                    backup_sha256=backup.sha256,
                    readback_path=readback_path,
                    readback_sha256=readback_sha256,
                    matched=readback_sha256 == backup.sha256,
                )
            )
        self.restore_records = records
        self.restored = True
        self.write_restore_manifest(self.run_dir / "restore-verification.json", records)
        if not all(record.matched for record in records):
            raise RestoreError("restore readback verification failed")
        return records

    @staticmethod
    def write_restore_manifest(path: Path, records: list[RestoreRecord]) -> None:
        payload = {
            "created_utc": utc_now(),
            "restore_verified": all(record.matched for record in records),
            "regions": [record.to_json() for record in records],
        }
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
