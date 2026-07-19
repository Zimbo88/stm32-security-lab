"""Command-line interface for the secure-boot HIL framework."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .config import DEFAULT_CONFIG, HilConfig, load_config
from .errors import HilError
from .runner import SecureBootHilRunner, create_run_dir, write_empty_restore_files

CONFIRMATION_PHRASE = "I UNDERSTAND THIS WILL MODIFY STM32 FLASH"


def _csv_set(value: str | None) -> set[str] | None:
    if not value:
        return None
    return {item.strip() for item in value.split(",") if item.strip()}


def _load_config(args: argparse.Namespace) -> HilConfig:
    return load_config(
        repo_root=args.repo_root,
        config_path=args.config,
        output_root=args.output,
        uart_device=args.uart,
        baud=args.baud,
        capture_seconds=args.capture_seconds,
        reset_cycles=args.reset_cycles,
    )


def _confirm_if_required(args: argparse.Namespace) -> None:
    if not (args.require_confirmation or args.keep_test_state):
        return
    if args.non_interactive:
        raise HilError(
            "--keep-test-state and --require-confirmation require an interactive confirmation"
        )
    print(CONFIRMATION_PHRASE)
    observed = input("Type the confirmation phrase exactly: ")
    if observed != CONFIRMATION_PHRASE:
        raise HilError("confirmation phrase did not match")


def run_command(args: argparse.Namespace) -> int:
    config = _load_config(args)
    runner = SecureBootHilRunner(config)
    tests = runner.planned_tests(
        identifiers=_csv_set(args.tests),
        tags=_csv_set(args.tags),
        exclude_tags=_csv_set(args.exclude_tags),
    )
    run_dir = create_run_dir(config.output_root)
    if args.dry_run:
        write_empty_restore_files(run_dir)
        exit_code = runner.write_dry_run(
            run_dir=run_dir,
            tests=tests,
            strict_observations=args.strict_observations,
        )
    else:
        _confirm_if_required(args)
        exit_code = runner.run_suite(
            run_dir=run_dir,
            tests=tests,
            strict_observations=args.strict_observations,
            keep_test_state=args.keep_test_state,
            campaign_seed=args.campaign_seed,
            campaign_count=args.campaign_count,
        )
    print(run_dir)
    return exit_code


def validate_config_command(args: argparse.Namespace) -> int:
    config = _load_config(args)
    runner = SecureBootHilRunner(config)
    environment = runner.validate_config(dry_run=True)
    print(
        json.dumps(
            {"configuration": config.to_json(), "environment": environment},
            indent=2,
            sort_keys=True,
        )
    )
    return 0


def list_tests_command(args: argparse.Namespace) -> int:
    config = _load_config(args)
    runner = SecureBootHilRunner(config)
    tests = runner.planned_tests(
        identifiers=_csv_set(args.tests),
        tags=_csv_set(args.tags),
        exclude_tags=_csv_set(args.exclude_tags),
    )
    for test in tests:
        print(
            f"{test.identifier}\t{test.expected_verdict.value}\t{','.join(test.tags)}\t{test.name}"
        )
    return 0


def build_images_command(args: argparse.Namespace) -> int:
    config = _load_config(args)
    runner = SecureBootHilRunner(config)
    run_dir = create_run_dir(config.output_root)
    outputs, image_set = runner.build_images(
        run_dir=run_dir,
        campaign_seed=args.campaign_seed,
        campaign_count=args.campaign_count,
    )
    print(
        json.dumps(
            {
                "run_dir": run_dir.as_posix(),
                "bootloader": outputs.bootloader.to_json(),
                "slot_a": outputs.slot_a.to_json(),
                "slot_b": outputs.slot_b.to_json(),
                "mutations": [record.to_json() for record in image_set.mutation_records],
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


def inspect_backup_command(args: argparse.Namespace) -> int:
    manifest = args.run_directory / "backup-manifest.json"
    if not manifest.exists():
        raise HilError(f"backup manifest missing: {manifest}")
    print(manifest.read_text(encoding="utf-8"), end="")
    return 0


def report_command(args: argparse.Namespace) -> int:
    results = args.run_directory / "results.json"
    report = args.run_directory / "report.md"
    if not results.exists():
        raise HilError(f"results file missing: {results}")
    payload = json.loads(results.read_text(encoding="utf-8"))
    print(json.dumps(payload.get("counts", {}), indent=2, sort_keys=True))
    if report.exists():
        print(report)
    return 0


def add_common_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--config", type=Path, default=None, help=f"default: {DEFAULT_CONFIG}")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--uart", type=Path, default=None)
    parser.add_argument("--baud", type=int, default=None)
    parser.add_argument("--capture-seconds", type=float, default=None)
    parser.add_argument("--reset-cycles", type=int, default=None)


def add_selection_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--tests", help="comma-separated stable test identifiers")
    parser.add_argument("--tags", help="comma-separated tags to include")
    parser.add_argument("--exclude-tags", help="comma-separated tags to exclude")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="secure-boot-hil",
        description="STM32 secure-boot hardware-in-the-loop validation framework",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    run = subparsers.add_parser("run", help="execute the HIL suite")
    add_common_options(run)
    add_selection_options(run)
    run.add_argument("--dry-run", action="store_true")
    run.add_argument("--non-interactive", action="store_true")
    run.add_argument("--require-confirmation", action="store_true")
    run.add_argument("--strict-observations", action="store_true")
    run.add_argument("--campaign-seed")
    run.add_argument("--campaign-count", type=int, default=0)
    run.add_argument("--log-level", default="INFO")
    run.add_argument("--keep-test-state", action="store_true")
    run.set_defaults(func=run_command)

    validate = subparsers.add_parser("validate-config", help="validate HIL configuration")
    add_common_options(validate)
    validate.set_defaults(func=validate_config_command)

    list_tests = subparsers.add_parser("list-tests", help="list available HIL tests")
    add_common_options(list_tests)
    add_selection_options(list_tests)
    list_tests.set_defaults(func=list_tests_command)

    build_images = subparsers.add_parser(
        "build-images", help="build and cache valid and fault images"
    )
    add_common_options(build_images)
    build_images.add_argument("--campaign-seed")
    build_images.add_argument("--campaign-count", type=int, default=0)
    build_images.set_defaults(func=build_images_command)

    inspect_backup = subparsers.add_parser(
        "inspect-backup", help="print backup manifest from a run"
    )
    inspect_backup.add_argument("run_directory", type=Path)
    inspect_backup.set_defaults(func=inspect_backup_command)

    report = subparsers.add_parser("report", help="summarize an existing run report")
    report.add_argument("run_directory", type=Path)
    report.set_defaults(func=report_command)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except HilError as exc:
        print(f"secure-boot-hil: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
