"""Flash backup manifest generation."""

from __future__ import annotations

import json
from pathlib import Path

from .build import sha256_file
from .config import HilConfig
from .errors import FlashError
from .flash import FlashAdapter
from .logging import utc_now
from .model import BackupRecord

BACKUP_REGION_NAMES = ("bootloader", "metadata_a", "metadata_b", "slot_a", "slot_b")


class BackupManager:
    def __init__(self, config: HilConfig, flash: FlashAdapter) -> None:
        self.config = config
        self.flash = flash

    def backup_all(self, run_dir: Path) -> list[BackupRecord]:
        backup_dir = run_dir / "flash-backup"
        backup_dir.mkdir(parents=True, exist_ok=True)
        records: list[BackupRecord] = []
        for region_name in BACKUP_REGION_NAMES:
            region = self.config.region(region_name)
            path = backup_dir / f"{region.name}.bin"
            self.flash.read_region(region, path)
            actual_size = path.stat().st_size
            if actual_size != region.size:
                raise FlashError(
                    f"backup for {region.name} has {actual_size} bytes, expected {region.size}"
                )
            records.append(
                BackupRecord(
                    region=region,
                    path=path,
                    size=actual_size,
                    sha256=sha256_file(path),
                    created_utc=utc_now(),
                )
            )
        self.write_manifest(run_dir / "backup-manifest.json", records)
        return records

    @staticmethod
    def write_manifest(path: Path, records: list[BackupRecord]) -> None:
        payload = {
            "created_utc": utc_now(),
            "backups": [record.to_json() for record in records],
        }
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
