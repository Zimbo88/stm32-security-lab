"""Firmware build orchestration and immutable artifact caching."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .config import HilConfig
from .errors import ProcessError
from .logging import utc_now
from .model import Artifact, CommandResult
from .process import ProcessRunner


@dataclass(frozen=True)
class BuildOutputs:
    bootloader: Artifact
    slot_a: Artifact
    slot_b: Artifact
    manifest_path: Path
    commands: tuple[CommandResult, ...]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def artifact_from_path(path: Path, *, name: str, role: str) -> Artifact:
    if not path.exists():
        raise ProcessError(f"expected build artifact missing: {path}")
    size = path.stat().st_size
    if size <= 0:
        raise ProcessError(f"expected build artifact is empty: {path}")
    return Artifact(name=name, path=path, size=size, sha256=sha256_file(path), role=role)


class BuildOrchestrator:
    def __init__(self, config: HilConfig, runner: ProcessRunner) -> None:
        self.config = config
        self.runner = runner
        self.commands: list[CommandResult] = []
        self.log_dir: Path | None = None
        self.log_paths: list[Path] = []

    def _log_path(self, label: str) -> Path:
        if self.log_dir is None:
            raise ProcessError("build log directory is not initialized")
        safe_label = re.sub(r"[^A-Za-z0-9_.-]+", "_", label).strip("_")
        return self.log_dir / f"{len(self.log_paths) + 1:02d}_{safe_label}.log"

    @staticmethod
    def _write_command_log(path: Path, result: CommandResult) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            "\n".join(
                [
                    f"command: {' '.join(result.command)}",
                    f"cwd: {result.cwd}",
                    f"start_utc: {result.start_utc}",
                    f"end_utc: {result.end_utc}",
                    f"duration_seconds: {result.duration_seconds:.6f}",
                    f"returncode: {result.returncode}",
                    f"timed_out: {result.timed_out}",
                    "",
                    "=== stdout ===",
                    result.stdout,
                    "",
                    "=== stderr ===",
                    result.stderr,
                    "",
                ]
            ),
            encoding="utf-8",
        )

    @staticmethod
    def _tail(path: Path, line_count: int = 100) -> str:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        return "\n".join(lines[-line_count:]) if lines else "<no subprocess output>"

    def _format_failure(self, command: list[str], log_path: Path) -> str:
        return "\n".join(
            [
                f"build command failed: {' '.join(command)}",
                f"full build log: {log_path}",
                "final 100 build output lines:",
                self._tail(log_path),
            ]
        )

    def _run(self, command: list[str], *, label: str) -> None:
        log_path = self._log_path(label)
        try:
            result = self.runner.run(
                command,
                cwd=self.config.repo_root,
                timeout_seconds=self.config.command_timeout_seconds,
                check=False,
            )
        except ProcessError as exc:
            if exc.result is not None:
                self._write_command_log(log_path, exc.result)
            else:
                log_path.parent.mkdir(parents=True, exist_ok=True)
                log_path.write_text(
                    "\n".join(
                        [
                            f"command: {' '.join(command)}",
                            f"cwd: {self.config.repo_root.as_posix()}",
                            "",
                            "=== framework error ===",
                            str(exc),
                            "",
                        ]
                    ),
                    encoding="utf-8",
                )
            self.log_paths.append(log_path)
            raise ProcessError(self._format_failure(command, log_path), result=exc.result) from exc

        self.commands.append(result)
        self._write_command_log(log_path, result)
        self.log_paths.append(log_path)
        if result.returncode != 0:
            raise ProcessError(self._format_failure(command, log_path), result=result)

    def _copy_artifact(self, source: Path, destination: Path, *, name: str, role: str) -> Artifact:
        artifact_from_path(source, name=name, role=role)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        return artifact_from_path(destination, name=name, role=role)

    def _app_make_args(self, slot: str) -> list[str]:
        return [
            self.config.make,
            "-C",
            "firmware/exp066_research_platform_core",
            f"SLOT={slot}",
            "clean",
            "all",
            "verify-signed",
            f"LAYOUT_PROFILE={self.config.layout_profile}",
            f"SIGNING_SEED={self.config.signing_seed}",
            f"PUBLIC_KEY_HEADER={self.config.public_key_header}",
        ]

    def planned_commands(self) -> list[list[str]]:
        return [
            [
                self.config.make,
                "-C",
                "firmware/exp045_bootloader_v2",
                "clean",
                "all",
                "report",
                f"LAYOUT_PROFILE={self.config.layout_profile}",
            ],
            self._app_make_args("a"),
            self._app_make_args("b"),
        ]

    def build_and_cache(self, run_dir: Path) -> BuildOutputs:
        cache_dir = run_dir / "image-cache"
        self.log_dir = run_dir / "build-logs"
        cache_dir.mkdir(parents=True, exist_ok=True)

        self._run(self.planned_commands()[0], label="bootloader")
        bootloader = self._copy_artifact(
            self.config.repo_root / "firmware/exp045_bootloader_v2/build/exp045_bootloader_v2.bin",
            cache_dir / "bootloader.bin",
            name="bootloader",
            role="bootloader",
        )

        self._run(self._app_make_args("a"), label="slot_a")
        slot_a_source = (
            self.config.repo_root / "firmware/exp066_research_platform_core/build/"
            "exp066_research_platform_core_slot_a_update_v2.bin"
        )
        slot_a = self._copy_artifact(
            slot_a_source,
            cache_dir / "slot_a_update_v2.bin",
            name="slot_a_update_v2",
            role="slot_a",
        )

        self._run(self._app_make_args("b"), label="slot_b")
        slot_b_source = (
            self.config.repo_root / "firmware/exp066_research_platform_core/build/"
            "exp066_research_platform_core_slot_b_update_v2.bin"
        )
        slot_b = self._copy_artifact(
            slot_b_source,
            cache_dir / "slot_b_update_v2.bin",
            name="slot_b_update_v2",
            role="slot_b",
        )

        manifest = {
            "created_utc": utc_now(),
            "artifacts": [
                bootloader.to_json(),
                slot_a.to_json(),
                slot_b.to_json(),
            ],
            "commands": [command.to_json() for command in self.commands],
            "build_logs": [path.as_posix() for path in self.log_paths],
            "slot_a_cache_rule": (
                "Slot A package was copied to image-cache before the Slot B clean build started."
            ),
        }
        manifest_path = cache_dir / "image-manifest.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return BuildOutputs(
            bootloader=bootloader,
            slot_a=slot_a,
            slot_b=slot_b,
            manifest_path=manifest_path,
            commands=tuple(self.commands),
        )

    @staticmethod
    def write_manifest(path: Path, artifacts: list[Artifact]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        payload: dict[str, Any] = {
            "created_utc": utc_now(),
            "artifacts": [artifact.to_json() for artifact in artifacts],
        }
        path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
