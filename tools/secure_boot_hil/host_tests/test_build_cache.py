from __future__ import annotations

import shutil
from pathlib import Path

import pytest

from secure_boot_hil.build import BuildOrchestrator
from secure_boot_hil.config import HilConfig
from secure_boot_hil.errors import ProcessError
from secure_boot_hil.model import CommandResult, FlashRegion
from secure_boot_hil.process import ProcessRunner


def command_result(command: list[str], cwd: Path) -> CommandResult:
    return CommandResult(
        command=command,
        cwd=cwd.as_posix(),
        start_utc="2026-01-01T00:00:00Z",
        end_utc="2026-01-01T00:00:00Z",
        duration_seconds=0.0,
        returncode=0,
        stdout="",
        stderr="",
    )


class FakeBuildRunner(ProcessRunner):
    def __init__(self, repo_root: Path, run_dir: Path) -> None:
        self.repo_root = repo_root
        self.run_dir = run_dir
        self.commands: list[list[str]] = []

    def run(
        self,
        command: list[str],
        *,
        cwd: Path,
        timeout_seconds: float | None = None,
        check: bool = True,
    ) -> CommandResult:
        self.commands.append(command)
        boot_build = self.repo_root / "firmware/exp045_bootloader_v2/build"
        app_build = self.repo_root / "firmware/exp066_research_platform_core/build"
        if "firmware/exp045_bootloader_v2" in command:
            boot_build.mkdir(parents=True, exist_ok=True)
            (boot_build / "exp045_bootloader_v2.bin").write_bytes(b"bootloader")
        elif "SLOT=a" in command:
            app_build.mkdir(parents=True, exist_ok=True)
            (app_build / "exp066_research_platform_core_slot_a_update_v2.bin").write_bytes(
                b"slot-a"
            )
        elif "SLOT=b" in command:
            assert (self.run_dir / "image-cache" / "slot_a_update_v2.bin").read_bytes() == b"slot-a"
            shutil.rmtree(app_build, ignore_errors=True)
            app_build.mkdir(parents=True, exist_ok=True)
            (app_build / "exp066_research_platform_core_slot_b_update_v2.bin").write_bytes(
                b"slot-b"
            )
        return command_result(command, cwd)


class FailingSlotABuildRunner(FakeBuildRunner):
    def run(
        self,
        command: list[str],
        *,
        cwd: Path,
        timeout_seconds: float | None = None,
        check: bool = True,
    ) -> CommandResult:
        if "SLOT=a" not in command:
            return super().run(
                command,
                cwd=cwd,
                timeout_seconds=timeout_seconds,
                check=check,
            )
        stdout = "\n".join(f"slot-a-line-{index:03d}" for index in range(150))
        return CommandResult(
            command=command,
            cwd=cwd.as_posix(),
            start_utc="2026-01-01T00:00:00Z",
            end_utc="2026-01-01T00:00:00Z",
            duration_seconds=0.1,
            returncode=2,
            stdout=stdout,
            stderr="make: slot a failed",
        )


def make_config(repo_root: Path, output_root: Path) -> HilConfig:
    return HilConfig(
        repo_root=repo_root,
        output_root=output_root,
        uart_device=Path("/dev/null"),
        baud=115200,
        capture_seconds=5.0,
        reset_cycles=2,
        layout_profile="stm32f429_1m",
        signing_seed=repo_root / "seed.bin",
        public_key_header=repo_root / "public.h",
        st_flash="st-flash",
        make="make",
        flash_base=0x08000000,
        flash_end=0x08100000,
        regions=(FlashRegion("slot_a", 0x08020000, 0x60000),),
        command_timeout_seconds=1.0,
    )


def test_slot_a_cache_survives_slot_b_clean(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    run_dir = tmp_path / "run"
    config = make_config(repo_root, tmp_path / "out")
    runner = FakeBuildRunner(repo_root, run_dir)

    outputs = BuildOrchestrator(config, runner).build_and_cache(run_dir)

    assert outputs.slot_a.path.read_bytes() == b"slot-a"
    assert outputs.slot_b.path.read_bytes() == b"slot-b"
    assert not (
        repo_root / "firmware/exp066_research_platform_core/build/"
        "exp066_research_platform_core_slot_a_update_v2.bin"
    ).exists()


def test_slot_a_build_failure_reports_log_path_and_tail(tmp_path: Path) -> None:
    repo_root = tmp_path / "repo"
    run_dir = tmp_path / "run"
    config = make_config(repo_root, tmp_path / "out")
    runner = FailingSlotABuildRunner(repo_root, run_dir)

    with pytest.raises(ProcessError) as exc_info:
        BuildOrchestrator(config, runner).build_and_cache(run_dir)

    message = str(exc_info.value)
    log_path = run_dir / "build-logs" / "02_slot_a.log"
    assert f"full build log: {log_path}" in message
    assert "final 100 build output lines:" in message
    assert "slot-a-line-149" in message
    assert "slot-a-line-000" not in message
    assert log_path.exists()
    assert "slot-a-line-000" in log_path.read_text(encoding="utf-8")
