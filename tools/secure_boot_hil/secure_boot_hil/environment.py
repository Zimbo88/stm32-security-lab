"""Host and repository environment checks."""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path
from typing import Any

from .config import HilConfig
from .errors import EnvironmentError

REQUIRED_REPO_PATHS = (
    "firmware/exp045_bootloader_v2/Makefile",
    "firmware/exp066_research_platform_core/Makefile",
    "firmware/exp045_bootloader_v2/src/firmware_public_key.h",
    "tools/update_package.py",
    "tools/stm32f429_layout.py",
)


def git_value(repo_root: Path, args: list[str]) -> str | None:
    try:
        return subprocess.run(
            ["git", *args],
            cwd=repo_root,
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def tool_version(command: str) -> str:
    resolved = shutil.which(command)
    if resolved is None:
        return "missing"
    try:
        result = subprocess.run(
            [command, "--version"],
            text=True,
            capture_output=True,
            check=False,
        )
    except OSError as exc:
        return f"error: {exc}"
    first_line = (result.stdout or result.stderr).splitlines()
    return first_line[0] if first_line else f"{command} available"


def inspect_environment(config: HilConfig) -> dict[str, Any]:
    return {
        "git": {
            "commit": git_value(config.repo_root, ["rev-parse", "HEAD"]),
            "branch": git_value(config.repo_root, ["branch", "--show-current"]),
            "dirty": bool(git_value(config.repo_root, ["status", "--porcelain"])),
        },
        "tools": {
            "make": tool_version(config.make),
            "st_flash": tool_version(config.st_flash),
            "python": tool_version("python3"),
            "arm_none_eabi_gcc": tool_version("arm-none-eabi-gcc"),
            "arm_none_eabi_readelf": tool_version("arm-none-eabi-readelf"),
        },
        "uart": {
            "device": config.uart_device.as_posix(),
            "baud": config.baud,
        },
    }


def validate_host_prerequisites(
    config: HilConfig,
    *,
    dry_run: bool,
    require_hardware: bool = False,
    require_signing_seed: bool = False,
) -> None:
    if not config.repo_root.is_dir():
        raise EnvironmentError(f"repo root does not exist: {config.repo_root}")
    for relative in REQUIRED_REPO_PATHS:
        path = config.repo_root / relative
        if not path.exists():
            raise EnvironmentError(f"required repository path missing: {relative}")
    if shutil.which(config.make) is None:
        raise EnvironmentError(f"required build tool missing: {config.make}")
    if require_hardware and not dry_run and shutil.which(config.st_flash) is None:
        raise EnvironmentError(f"required flash tool missing: {config.st_flash}")
    if require_hardware and not dry_run and not config.uart_device.exists():
        raise EnvironmentError(f"UART device missing: {config.uart_device}")
    if require_signing_seed and not dry_run and not config.signing_seed.exists():
        raise EnvironmentError(f"signing seed missing: {config.signing_seed}")
    if not config.public_key_header.exists():
        raise EnvironmentError(f"public key header missing: {config.public_key_header}")
    config.output_root.mkdir(parents=True, exist_ok=True)
