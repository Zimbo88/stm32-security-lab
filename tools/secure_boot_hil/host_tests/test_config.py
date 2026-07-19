from __future__ import annotations

from pathlib import Path

import pytest

from secure_boot_hil.config import load_config, parse_int, validate_regions
from secure_boot_hil.errors import ConfigError
from secure_boot_hil.model import FlashRegion

ROOT = Path(__file__).resolve().parents[3]


def test_default_config_uses_authoritative_1m_layout(tmp_path: Path) -> None:
    config = load_config(repo_root=ROOT, output_root=tmp_path / "out")
    assert config.layout_profile == "stm32f429_1m"
    assert config.flash_base == 0x08000000
    assert config.flash_end == 0x08100000
    assert config.region("slot_a").address == 0x08020000
    assert config.region("slot_b").end == 0x080E0000


def test_numeric_address_parsing() -> None:
    assert parse_int("0x08020000", "address") == 0x08020000
    assert parse_int(1024, "size") == 1024
    with pytest.raises(ConfigError):
        parse_int("-1", "address")
    with pytest.raises(ConfigError):
        parse_int("not-an-integer", "address")


def test_region_overlap_detection() -> None:
    regions = (
        FlashRegion("left", 0x08000000, 0x1000),
        FlashRegion("right", 0x08000800, 0x1000),
    )
    with pytest.raises(ConfigError, match="overlap"):
        validate_regions(regions, flash_base=0x08000000, flash_end=0x08100000)


def test_flash_boundary_detection() -> None:
    regions = (FlashRegion("outside", 0x080FF000, 0x2000),)
    with pytest.raises(ConfigError, match="outside"):
        validate_regions(regions, flash_base=0x08000000, flash_end=0x08100000)
