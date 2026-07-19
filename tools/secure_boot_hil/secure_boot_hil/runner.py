"""HIL suite orchestration."""

from __future__ import annotations

import json
import signal
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .assertions import evaluate_assertions
from .backup import BACKUP_REGION_NAMES
from .build import BuildOrchestrator, BuildOutputs, artifact_from_path
from .config import HilConfig, load_repo_layout, write_json
from .environment import inspect_environment, validate_host_prerequisites
from .errors import HilError, RestoreError
from .flash import FlashAdapter, StFlash
from .images import (
    inspect_signed_artifact,
    mutate_region_fill,
    mutate_signed_image,
    mutation_manifest,
)
from .logging import RunLogger, utc_now
from .metrics import parse_boot_metrics
from .model import (
    Artifact,
    FlashAction,
    MutationRecord,
    MutationType,
    TestCase,
    TestResult,
    UartFrame,
    Verdict,
)
from .process import ProcessRunner
from .reporting import Reporter, suite_exit_code
from .restore import RestoreTransaction
from .testspec import filter_tests, initial_catalog
from .uart import UartCollector


@dataclass(frozen=True)
class ImageSet:
    artifacts: dict[str, Path]
    artifact_records: list[Artifact]
    mutation_records: list[MutationRecord]


def create_run_dir(output_root: Path) -> Path:
    stamp = utc_now().replace(":", "").replace("-", "").replace("Z", "Z")
    run_dir = output_root / f"run-{stamp}"
    run_dir.mkdir(parents=True, exist_ok=False)
    return run_dir


class SecureBootHilRunner:
    def __init__(
        self,
        config: HilConfig,
        *,
        process_runner: ProcessRunner | None = None,
        flash: FlashAdapter | None = None,
    ) -> None:
        self.config = config
        self.process_runner = process_runner or ProcessRunner()
        self.flash = flash or StFlash(config, self.process_runner)
        self.layout = load_repo_layout(config.repo_root, config.layout_profile)

    def validate_config(self, *, dry_run: bool = True) -> dict[str, Any]:
        validate_host_prerequisites(self.config, dry_run=dry_run)
        return inspect_environment(self.config)

    def planned_tests(
        self,
        *,
        identifiers: set[str] | None = None,
        tags: set[str] | None = None,
        exclude_tags: set[str] | None = None,
    ) -> list[TestCase]:
        return filter_tests(
            initial_catalog(reset_cycles=self.config.reset_cycles),
            identifiers=identifiers,
            tags=tags,
            exclude_tags=exclude_tags,
        )

    def write_dry_run(
        self,
        *,
        run_dir: Path,
        tests: list[TestCase],
        strict_observations: bool,
    ) -> int:
        validate_host_prerequisites(self.config, dry_run=True)
        run_logger = RunLogger(run_dir / "run.log")
        run_logger.event("dry_run_start", run_dir=run_dir.as_posix())
        environment = inspect_environment(self.config)
        reporter = Reporter(run_dir)
        reporter.write_static_inputs(config=self.config, environment=environment, tests=tests)
        build = BuildOrchestrator(self.config, self.process_runner)
        write_json(
            run_dir / "planned-build-commands.json",
            {"commands": build.planned_commands()},
        )
        write_json(
            run_dir / "image-manifest.json",
            {
                "schema_version": 1,
                "dry_run": True,
                "artifacts": [],
                "note": "No images are built or flashed during dry-run execution.",
            },
        )
        reporter.write_results(
            results=[],
            mutations=[],
            restore_verified=True,
            suite_start_utc=utc_now(),
        )
        run_logger.event("dry_run_end", test_count=len(tests))
        return suite_exit_code(
            [],
            restore_verified=True,
            reports_written=True,
            strict_observations=strict_observations,
        )

    def build_images(
        self,
        *,
        run_dir: Path,
        campaign_seed: str | None = None,
        campaign_count: int = 0,
    ) -> tuple[BuildOutputs, ImageSet]:
        if campaign_count < 0:
            raise HilError("campaign count must not be negative")
        if campaign_count > 0 and campaign_seed is None:
            raise HilError("campaign count requires --campaign-seed")
        validate_host_prerequisites(
            self.config,
            dry_run=False,
            require_hardware=False,
            require_signing_seed=True,
        )
        outputs = BuildOrchestrator(self.config, self.process_runner).build_and_cache(run_dir)
        image_set = self._prepare_signed_fault_images(
            run_dir,
            outputs,
            campaign_seed=campaign_seed,
            campaign_count=campaign_count,
        )
        return outputs, image_set

    def _prepare_signed_fault_images(
        self,
        run_dir: Path,
        outputs: BuildOutputs,
        *,
        campaign_seed: str | None,
        campaign_count: int,
    ) -> ImageSet:
        cache_dir = run_dir / "image-cache"
        fault_dir = cache_dir / "faults"
        artifacts: dict[str, Path] = {
            "bootloader": outputs.bootloader.path,
            "slot_a_valid": outputs.slot_a.path,
            "slot_b_valid": outputs.slot_b.path,
        }
        artifact_records: list[Artifact] = [outputs.bootloader, outputs.slot_a, outputs.slot_b]
        mutations: list[MutationRecord] = []
        self._prepare_confirmed_metadata_images(run_dir, artifacts, artifact_records)

        for slot, source in (("slot_a", outputs.slot_a.path), ("slot_b", outputs.slot_b.path)):
            for mutation_type, suffix in (
                (MutationType.BAD_SIGNATURE, "bad_signature"),
                (MutationType.MODIFIED_PAYLOAD, "modified_payload"),
                (MutationType.ERASED_HEADER, "erased_header"),
                (MutationType.ZERO_HEADER, "zero_header"),
            ):
                name = f"{slot}_{suffix}"
                output = fault_dir / f"{name}.bin"
                record = mutate_signed_image(source, output, mutation_type, self.layout)
                mutations.append(record)
                artifacts[name] = output
                artifact_records.append(artifact_from_path(output, name=name, role=f"{slot}_fault"))
            for index in range(campaign_count):
                name = f"{slot}_seeded_{index:04d}"
                output = fault_dir / f"{name}.bin"
                record = mutate_signed_image(
                    source,
                    output,
                    MutationType.SEEDED_BIT_FLIP,
                    self.layout,
                    seed=campaign_seed,
                    campaign_index=index,
                )
                mutations.append(record)
                artifacts[name] = output
                artifact_records.append(
                    artifact_from_path(output, name=name, role=f"{slot}_seeded_fault")
                )

        write_json(
            run_dir / "image-manifest.json",
            {
                "schema_version": 1,
                "created_utc": utc_now(),
                "signed_artifacts": [
                    inspect_signed_artifact(outputs.slot_a.path, self.layout),
                    inspect_signed_artifact(outputs.slot_b.path, self.layout),
                ],
                "artifacts": [artifact.to_json() for artifact in artifact_records],
            },
        )
        mutation_manifest(run_dir / "mutation-manifest.json", mutations)
        return ImageSet(
            artifacts=artifacts, artifact_records=artifact_records, mutation_records=mutations
        )

    def _prepare_confirmed_metadata_images(
        self,
        run_dir: Path,
        artifacts: dict[str, Path],
        artifact_records: list[Artifact],
    ) -> None:
        self.process_runner.run(
            [self.config.make, "-C", "tools", "boot-metadata-provision"],
            cwd=self.config.repo_root,
            timeout_seconds=self.config.command_timeout_seconds,
            check=True,
        )
        tool = self.config.repo_root / "tools" / "build" / "boot_metadata_provision.bin"
        metadata_dir = run_dir / "image-cache" / "metadata"
        metadata_dir.mkdir(parents=True, exist_ok=True)
        for slot in ("a", "b"):
            copy_a = metadata_dir / f"confirmed_slot_{slot}_copy_a.bin"
            copy_b = metadata_dir / f"confirmed_slot_{slot}_copy_b.bin"
            report = metadata_dir / f"confirmed_slot_{slot}.json"
            self.process_runner.run(
                [
                    tool.as_posix(),
                    "create-confirmed",
                    "--slot",
                    slot,
                    "--image-version",
                    "2",
                    "--copy-a-output",
                    copy_a.as_posix(),
                    "--copy-b-output",
                    copy_b.as_posix(),
                    "--sector-image",
                    "--json-output",
                    report.as_posix(),
                ],
                cwd=self.config.repo_root,
                timeout_seconds=self.config.command_timeout_seconds,
                check=True,
            )
            for copy_name, path in (("copy_a", copy_a), ("copy_b", copy_b)):
                name = f"metadata_slot_{slot}_{copy_name}"
                artifacts[name] = path
                artifact_records.append(
                    artifact_from_path(path, name=name, role=f"metadata_slot_{slot}")
                )

    def run_suite(
        self,
        *,
        run_dir: Path,
        tests: list[TestCase],
        strict_observations: bool = False,
        keep_test_state: bool = False,
        campaign_seed: str | None = None,
        campaign_count: int = 0,
    ) -> int:
        validate_host_prerequisites(
            self.config,
            dry_run=False,
            require_hardware=True,
            require_signing_seed=True,
        )
        run_logger = RunLogger(run_dir / "run.log")
        run_logger.event("suite_start", run_dir=run_dir.as_posix())
        suite_start = utc_now()
        environment = inspect_environment(self.config)
        reporter = Reporter(run_dir)
        reporter.write_static_inputs(config=self.config, environment=environment, tests=tests)
        _outputs, image_set = self.build_images(
            run_dir=run_dir,
            campaign_seed=campaign_seed,
            campaign_count=campaign_count,
        )
        results: list[TestResult] = []
        restore_verified = False
        reports_written = False

        with RestoreTransaction(
            self.config,
            self.flash,
            run_dir,
            enabled=True,
            keep_test_state=keep_test_state,
        ) as transaction:
            original_handlers = self._install_signal_restore(transaction, run_logger)
            try:
                self._add_metadata_fault_images(run_dir, image_set, transaction)
                for test in tests:
                    result = self._run_test(test, image_set, transaction, run_logger, run_dir)
                    results.append(result)
                    if not keep_test_state:
                        transaction.restore()
                if keep_test_state:
                    restore_verified = False
                else:
                    restore_verified = all(record.matched for record in transaction.restore())
            finally:
                self._restore_signal_handlers(original_handlers)

        if not keep_test_state:
            restore_path = run_dir / "restore-verification.json"
            if restore_path.exists():
                restore_verified = bool(
                    json.loads(restore_path.read_text(encoding="utf-8")).get("restore_verified")
                )
        try:
            reporter.write_results(
                results=results,
                mutations=image_set.mutation_records,
                restore_verified=restore_verified,
                suite_start_utc=suite_start,
            )
            reports_written = True
        except OSError as exc:
            run_logger.event("report_write_failed", error=str(exc))
        run_logger.event(
            "suite_end",
            restore_verified=restore_verified,
            result_counts={
                result.verdict.value: sum(1 for item in results if item.verdict == result.verdict)
                for result in results
            },
        )
        return suite_exit_code(
            results,
            restore_verified=restore_verified,
            reports_written=reports_written,
            strict_observations=strict_observations,
        )

    def _add_metadata_fault_images(
        self,
        run_dir: Path,
        image_set: ImageSet,
        transaction: RestoreTransaction,
    ) -> None:
        backup_by_region = {backup.region.name: backup for backup in transaction.backups}
        for region_name in ("metadata_a", "metadata_b"):
            backup = backup_by_region[region_name]
            for value, suffix, mutation_type in (
                (0xFF, "erased", MutationType.ERASED_METADATA),
                (0x00, "zero", MutationType.ZERO_METADATA),
            ):
                name = f"{region_name}_{suffix}"
                output = run_dir / "image-cache" / "faults" / f"{name}.bin"
                record = mutate_region_fill(
                    backup.path,
                    output,
                    mutation_type,
                    fill=value,
                    rationale=f"Fill {region_name} with 0x{value:02X} for redundancy testing.",
                )
                image_set.mutation_records.append(record)
                image_set.artifacts[name] = output
                image_set.artifact_records.append(
                    artifact_from_path(output, name=name, role=f"{region_name}_fault")
                )
        mutation_manifest(run_dir / "mutation-manifest.json", image_set.mutation_records)

    def _run_test(
        self,
        test: TestCase,
        image_set: ImageSet,
        transaction: RestoreTransaction,
        run_logger: RunLogger,
        run_dir: Path,
    ) -> TestResult:
        start = time.monotonic()
        start_utc = utc_now()
        frame: UartFrame | None = None
        required = []
        forbidden = []
        metrics: dict[str, float | int] = {}
        try:
            self.flash.write_region(
                self.config.region("bootloader"), image_set.artifacts["bootloader"]
            )
            for action in test.flash_actions:
                self._apply_action(action, image_set, run_logger, run_dir)

            captures = self.config.reset_cycles if test.identifier == "SB-STABILITY-001" else 1
            all_required = []
            all_forbidden = []
            for index in range(captures):
                collector = UartCollector(self.config)
                frame = collector.capture_after_reset(
                    raw_path=self._uart_raw_path(run_dir, test, index),
                    frame_dir=self._uart_frame_dir(run_dir, test, index),
                    reset=self.flash.reset,
                )
                current_required, current_forbidden, matched = evaluate_assertions(
                    frame.text,
                    test.required_uart,
                    test.forbidden_uart,
                )
                all_required.extend(current_required)
                all_forbidden.extend(current_forbidden)
                metrics.update(parse_boot_metrics(frame.text))
                if test.expected_verdict != Verdict.OBSERVE and not matched:
                    break
            required = all_required
            forbidden = all_forbidden
            if test.expected_verdict == Verdict.OBSERVE:
                verdict = Verdict.OBSERVE
            else:
                assertions_ok = all(result.matched for result in required) and not any(
                    result.matched for result in forbidden
                )
                verdict = Verdict.PASS if assertions_ok else Verdict.FAIL
            error = None
        except Exception as exc:
            verdict = Verdict.ERROR
            error = str(exc)
            run_logger.event("test_error", identifier=test.identifier, error=error)
            if not transaction.keep_test_state:
                try:
                    transaction.restore()
                except RestoreError as restore_exc:
                    error = f"{error}; restore failed: {restore_exc}"

        return TestResult(
            test=test,
            verdict=verdict,
            required=required,
            forbidden=forbidden,
            uart_frame=frame,
            metrics=metrics,
            start_utc=start_utc,
            end_utc=utc_now(),
            duration_seconds=time.monotonic() - start,
            error=error,
        )

    def _apply_action(
        self,
        action: FlashAction,
        image_set: ImageSet,
        run_logger: RunLogger,
        run_dir: Path,
    ) -> None:
        region = self.config.region(action.region)
        if action.artifact is not None:
            source = image_set.artifacts.get(action.artifact)
            if source is None:
                raise HilError(f"unknown test artifact: {action.artifact}")
        elif action.fill is not None:
            source = self._fill_image(run_dir=run_dir, region_name=action.region, fill=action.fill)
        else:
            raise HilError("flash action must name an artifact or fill byte")
        run_logger.event(
            "flash_action",
            operation=action.operation.value,
            region=region.name,
            address=f"0x{region.address:08X}",
            source=source.as_posix(),
        )
        self.flash.write_region(region, source)

    def _fill_image(self, *, run_dir: Path, region_name: str, fill: int) -> Path:
        if not (0 <= fill <= 0xFF):
            raise HilError("fill byte must fit in uint8")
        region = self.config.region(region_name)
        path = run_dir / "image-cache" / "fills" / f"{region_name}_{fill:02x}.bin"
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.stat().st_size != region.size:
            path.write_bytes(bytes([fill]) * region.size)
        return path

    def _uart_raw_path(self, run_dir: Path, test: TestCase, index: int) -> Path:
        return run_dir / "uart" / "raw" / f"{test.identifier}_{index:03d}.bin"

    def _uart_frame_dir(self, run_dir: Path, test: TestCase, index: int) -> Path:
        return run_dir / "uart" / "frames" / f"{test.identifier}_{index:03d}"

    def _install_signal_restore(
        self,
        transaction: RestoreTransaction,
        run_logger: RunLogger,
    ) -> dict[int, Any]:
        original_handlers: dict[int, Any] = {}

        def handler(signum: int, _frame: Any) -> None:
            run_logger.event("signal_received", signal=signum)
            if not transaction.keep_test_state:
                transaction.restore()
            raise KeyboardInterrupt(f"received signal {signum}")

        for signum in (signal.SIGINT, signal.SIGTERM):
            original_handlers[signum] = signal.getsignal(signum)
            signal.signal(signum, handler)
        return original_handlers

    @staticmethod
    def _restore_signal_handlers(original_handlers: dict[int, Any]) -> None:
        for signum, handler in original_handlers.items():
            signal.signal(signum, handler)


def write_empty_restore_files(run_dir: Path) -> None:
    write_json(run_dir / "backup-manifest.json", {"schema_version": 1, "backups": []})
    write_json(
        run_dir / "restore-verification.json",
        {"schema_version": 1, "restore_verified": True, "regions": []},
    )
    for directory in (
        "uart/raw",
        "uart/frames",
        "flash-backup",
        "restore-readback",
        "image-cache",
    ):
        (run_dir / directory).mkdir(parents=True, exist_ok=True)


def backup_region_names() -> tuple[str, ...]:
    return BACKUP_REGION_NAMES
