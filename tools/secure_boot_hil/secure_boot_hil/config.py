"""Configuration loading and safety validation."""

from __future__ import annotations

import importlib.util
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python 3.10 fallback
    import tomli as tomllib  # type: ignore[no-redef]

from .errors import ConfigError
from .model import FlashRegion

DEFAULT_CONFIG = Path(__file__).resolve().parents[1] / "config" / "default.toml"


@dataclass(frozen=True)
class HilConfig:
    repo_root: Path
    output_root: Path
    uart_device: Path
    baud: int
    capture_seconds: float
    reset_cycles: int
    layout_profile: str
    signing_seed: Path
    public_key_header: Path
    st_flash: str
    make: str
    flash_base: int
    flash_end: int
    regions: tuple[FlashRegion, ...]
    command_timeout_seconds: float

    def region(self, name: str) -> FlashRegion:
        for region in self.regions:
            if region.name == name:
                return region
        raise ConfigError(f"unknown flash region: {name}")

    def to_json(self) -> dict[str, Any]:
        return {
            "repo_root": self.repo_root.as_posix(),
            "output_root": self.output_root.as_posix(),
            "uart_device": self.uart_device.as_posix(),
            "baud": self.baud,
            "capture_seconds": self.capture_seconds,
            "reset_cycles": self.reset_cycles,
            "layout_profile": self.layout_profile,
            "signing_seed": self.signing_seed.as_posix(),
            "public_key_header": self.public_key_header.as_posix(),
            "st_flash": self.st_flash,
            "make": self.make,
            "flash_base": self.flash_base,
            "flash_base_hex": f"0x{self.flash_base:08X}",
            "flash_end": self.flash_end,
            "flash_end_hex": f"0x{self.flash_end:08X}",
            "regions": [region.to_json() for region in self.regions],
            "command_timeout_seconds": self.command_timeout_seconds,
        }


def parse_int(value: Any, field: str) -> int:
    try:
        if isinstance(value, int):
            parsed = value
        elif isinstance(value, str):
            parsed = int(value, 0)
        else:
            raise TypeError(type(value).__name__)
    except (TypeError, ValueError) as exc:
        raise ConfigError(f"{field} must be an integer address or size") from exc
    if parsed < 0:
        raise ConfigError(f"{field} must not be negative")
    return parsed


def _merge_dict(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    merged = dict(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = _merge_dict(merged[key], value)
        else:
            merged[key] = value
    return merged


def _load_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            return tomllib.load(stream)
    except OSError as exc:
        raise ConfigError(f"failed to read config {path}: {exc}") from exc
    except tomllib.TOMLDecodeError as exc:
        raise ConfigError(f"invalid TOML config {path}: {exc}") from exc


def load_repo_layout(repo_root: Path, profile: str) -> dict[str, Any]:
    layout_path = repo_root / "tools" / "stm32f429_layout.py"
    spec = importlib.util.spec_from_file_location("stm32f429_layout_for_hil", layout_path)
    if spec is None or spec.loader is None:
        raise ConfigError(f"failed to load layout helper from {layout_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.load_layout(profile=profile)


def regions_from_layout(layout: dict[str, Any]) -> tuple[FlashRegion, ...]:
    return (
        FlashRegion("bootloader", layout["bootloader_base"], layout["bootloader_size"]),
        FlashRegion("metadata_a", layout["boot_metadata_a_base"], layout["boot_metadata_a_size"]),
        FlashRegion("metadata_b", layout["boot_metadata_b_base"], layout["boot_metadata_b_size"]),
        FlashRegion("slot_a", layout["slot_a_signed_image_base"], layout["slot_a_size"]),
        FlashRegion("slot_b", layout["slot_b_signed_image_base"], layout["slot_b_size"]),
    )


def validate_regions(
    regions: tuple[FlashRegion, ...],
    *,
    flash_base: int,
    flash_end: int,
) -> None:
    if flash_base >= flash_end:
        raise ConfigError("flash base must be below flash end")
    ordered = sorted(regions, key=lambda region: region.address)
    for region in ordered:
        if region.size <= 0:
            raise ConfigError(f"region {region.name} has non-positive size")
        if region.address < flash_base or region.end > flash_end:
            raise ConfigError(f"region {region.name} is outside configured flash")
    for left, right in zip(ordered, ordered[1:], strict=False):
        if left.end > right.address:
            raise ConfigError(f"regions overlap: {left.name} and {right.name}")


def load_config(
    *,
    repo_root: Path,
    config_path: Path | None = None,
    output_root: Path | None = None,
    uart_device: Path | None = None,
    baud: int | None = None,
    capture_seconds: float | None = None,
    reset_cycles: int | None = None,
) -> HilConfig:
    raw = _load_toml(DEFAULT_CONFIG)
    if config_path is not None:
        raw = _merge_dict(raw, _load_toml(config_path))

    repo_root = repo_root.expanduser().resolve()
    layout_profile = str(raw.get("layout_profile", "stm32f429_1m"))
    layout = load_repo_layout(repo_root, layout_profile)
    flash_base = parse_int(layout["flash_base"], "flash_base")
    flash_end = parse_int(layout["flash_end"], "flash_end")
    regions = regions_from_layout(layout)
    validate_regions(regions, flash_base=flash_base, flash_end=flash_end)

    paths = raw.get("paths", {})
    tools = raw.get("tools", {})
    uart = raw.get("uart", {})
    execution = raw.get("execution", {})

    configured_output = output_root or Path(str(paths.get("output_root", "hil-results")))
    configured_uart = uart_device or Path(str(uart.get("device", "UART_DEVICE_NOT_CONFIGURED")))
    configured_baud = baud if baud is not None else int(uart.get("baud", 115200))
    configured_capture = (
        capture_seconds if capture_seconds is not None else float(uart.get("capture_seconds", 5.0))
    )
    configured_reset_cycles = (
        reset_cycles if reset_cycles is not None else int(execution.get("reset_cycles", 10))
    )

    if configured_baud <= 0:
        raise ConfigError("baud must be positive")
    if configured_capture <= 0:
        raise ConfigError("capture seconds must be positive")
    if configured_reset_cycles <= 0:
        raise ConfigError("reset cycles must be positive")

    signing_seed = repo_root / str(
        paths.get(
            "signing_seed",
            "firmware/exp065_signed_app/keys/firmware_signing_seed.bin",
        )
    )
    public_key_header = repo_root / str(
        paths.get(
            "public_key_header",
            "firmware/exp045_bootloader_v2/src/firmware_public_key.h",
        )
    )

    return HilConfig(
        repo_root=repo_root,
        output_root=(repo_root / configured_output).resolve()
        if not configured_output.is_absolute()
        else configured_output.resolve(),
        uart_device=configured_uart,
        baud=configured_baud,
        capture_seconds=configured_capture,
        reset_cycles=configured_reset_cycles,
        layout_profile=layout_profile,
        signing_seed=signing_seed,
        public_key_header=public_key_header,
        st_flash=str(tools.get("st_flash", "st-flash")),
        make=str(tools.get("make", "make")),
        flash_base=flash_base,
        flash_end=flash_end,
        regions=regions,
        command_timeout_seconds=float(execution.get("command_timeout_seconds", 120.0)),
    )


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
