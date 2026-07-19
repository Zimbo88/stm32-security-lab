from __future__ import annotations

from pathlib import Path

from secure_boot_hil.config import load_config
from secure_boot_hil.flash import FakeFlash
from secure_boot_hil.restore import RestoreTransaction

ROOT = Path(__file__).resolve().parents[3]


def test_restore_is_idempotent_and_verified(tmp_path: Path) -> None:
    config = load_config(repo_root=ROOT, output_root=tmp_path / "out")
    flash = FakeFlash(config)
    slot_a = config.region("slot_a")
    slot_offset = slot_a.address - config.flash_base
    flash.memory[slot_offset : slot_offset + 32] = b"A" * 32
    corrupt = tmp_path / "corrupt.bin"
    corrupt.write_bytes(b"B" * 32)

    with RestoreTransaction(config, flash, tmp_path) as transaction:
        flash.write_region(slot_a, corrupt)
        first = transaction.restore()
        second = transaction.restore()

    assert all(record.matched for record in first)
    assert all(record.matched for record in second)
    assert bytes(flash.memory[slot_offset : slot_offset + 32]) == b"A" * 32
    assert (tmp_path / "restore-verification.json").exists()
