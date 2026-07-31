from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any

from .client import DEFAULT_RESPONSE_TIMEOUT_SECONDS, Stm32Client, UpdateResult
from .errors import EXIT_FAILED, Stm32CtlError
from .package import PackageInfo, load_and_verify_package
from .protocol import Info, TargetStatus, status_name
from .transport import SerialPort

DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = DEFAULT_RESPONSE_TIMEOUT_SECONDS
DEFAULT_RETRIES = 1


def _info_dict(info: Info) -> dict[str, Any]:
    return {
        "protocol_version": info.protocol_version,
        "max_payload_size": info.max_payload_size,
        "max_write_data_size": info.max_write_data_size,
        "header_size": info.header_size,
        "crc_size": info.crc_size,
        "session_state": info.session_state,
    }


def _status_dict(status: TargetStatus) -> dict[str, Any]:
    return {
        "session_state": status.session_state,
        "last_status": status_name(status.last_status),
        "last_status_code": int(status.last_status),
        "expected_sequence": status.expected_sequence,
        "installer_state": status.installer_state,
        "accepted_payload_bytes": status.accepted_payload_bytes,
        "image_version": status.image_version,
        "programmed_block_count": status.programmed_block_count,
    }


def _package_dict(package: PackageInfo) -> dict[str, Any]:
    return {
        "path": package.path.as_posix(),
        "slot": package.slot,
        "image_version": package.image_version,
        "payload_size": package.payload_size,
        "package_size": package.package_size,
        "signed_header_size": package.signed_header_size,
        "vector_address": package.vector_address,
        "payload_sha512": package.payload_sha512,
    }


def _update_result_dict(result: UpdateResult) -> dict[str, Any]:
    return {
        "image_version": result.image_version,
        "payload_size": result.payload_size,
        "programmed_block_count": result.programmed_block_count,
        "final_status": _status_dict(result.final_status),
    }


def _print_json(data: dict[str, Any]) -> None:
    print(json.dumps(data, indent=2, sort_keys=True))


def _open_port(args: argparse.Namespace) -> SerialPort:
    port = SerialPort(args.port, args.baud, args.timeout)
    port.reset_input_buffer()
    return port


def _print_info(info: Info) -> None:
    print(f"protocol version : {info.protocol_version}")
    print(f"max payload      : {info.max_payload_size} bytes")
    print(f"max write data   : {info.max_write_data_size} bytes")
    print(f"header size      : {info.header_size} bytes")
    print(f"crc size         : {info.crc_size} bytes")
    print(f"session state    : {info.session_state}")


def _print_status(status: TargetStatus) -> None:
    print(f"session state    : {status.session_state}")
    print(f"last status      : {status_name(status.last_status)} ({int(status.last_status)})")
    print(f"expected seq     : {status.expected_sequence}")
    print(f"installer state  : {status.installer_state}")
    print(f"accepted payload : {status.accepted_payload_bytes} bytes")
    print(f"image version    : {status.image_version}")
    print(f"programmed blocks: {status.programmed_block_count}")


def run_info(args: argparse.Namespace) -> int:
    with _open_port(args) as transport:
        client = Stm32Client(transport, timeout=args.timeout, retries=args.retries)
        info = client.hello()
    if args.json:
        _print_json({"result": "ok", "info": _info_dict(info)})
    else:
        _print_info(info)
    return 0


def run_status(args: argparse.Namespace) -> int:
    with _open_port(args) as transport:
        client = Stm32Client(transport, timeout=args.timeout, retries=args.retries)
        client.hello()
        status = client.get_status()
    if args.json:
        _print_json({"result": "ok", "status": _status_dict(status)})
    else:
        _print_status(status)
    return 0


def run_reset(args: argparse.Namespace) -> int:
    with _open_port(args) as transport:
        client = Stm32Client(transport, timeout=args.timeout, retries=args.retries)
        client.hello()
        client.reset()
    if args.json:
        _print_json({"result": "ok", "reset": "requested"})
    else:
        print("reset requested")
    return 0


def _progress(enabled: bool) -> Callable[[int, int], None] | None:
    if not enabled:
        return None

    def report(done: int, total: int) -> None:
        percent = (done * 100) // total if total else 100
        sys.stderr.write(f"\rupdate: {done}/{total} bytes ({percent}%)")
        if done >= total:
            sys.stderr.write("\n")
        sys.stderr.flush()

    return report


def run_update(args: argparse.Namespace) -> int:
    package = load_and_verify_package(
        args.package,
        public_key_hex=args.public_key_hex,
        public_key_header=args.public_key_header,
    )

    if args.command == "recovery" and package.slot != "a":
        raise Stm32CtlError(
            "recovery bootstrap requires a signed Slot-A package"
        )

    if not args.quiet:
        print(
            f"verified package: slot {package.slot}, image version {package.image_version}, "
            f"{package.payload_size} payload bytes",
            file=sys.stderr,
        )

    with _open_port(args) as transport:
        client = Stm32Client(transport, timeout=args.timeout, retries=args.retries)
        result = client.update(
            package,
            block_size=args.block_size,
            progress=_progress(not args.quiet),
        )

    if args.json:
        _print_json(
            {
                "result": "ok",
                "package": _package_dict(package),
                "update": _update_result_dict(result),
            }
        )
    else:
        print(
            f"update complete: image version {result.image_version}, "
            f"{result.payload_size} payload bytes"
        )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="stm32ctl",
        description="Host tool for the EXP045 bootloader UART binary protocol",
    )
    parser.add_argument("--port", required=True, help="local serial port, for example /dev/ttyUSBx")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="UART baudrate")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="read timeout seconds")
    parser.add_argument(
        "--retries",
        type=int,
        default=DEFAULT_RETRIES,
        help="retries for idempotent read-only requests",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")

    subparsers = parser.add_subparsers(dest="command", required=True)

    info = subparsers.add_parser("info", help="query protocol information")
    info.set_defaults(func=run_info)

    status = subparsers.add_parser("status", help="query protocol and installer status")
    status.set_defaults(func=run_status)

    def add_update_arguments(command_parser: argparse.ArgumentParser) -> None:
        command_parser.add_argument(
            "--package", required=True, type=Path, help="update package file"
        )
        command_parser.add_argument(
            "--block-size", type=int, help="WRITE_BLOCK data bytes per frame"
        )
        command_parser.add_argument(
            "--public-key-hex", help="32-byte Ed25519 public key as hex"
        )
        command_parser.add_argument(
            "--public-key-header",
            type=Path,
            help="C header containing the bootloader Ed25519 public key",
        )
        command_parser.add_argument(
            "--quiet", action="store_true", help="suppress progress output"
        )

    update = subparsers.add_parser("update", help="stream a verified update package")
    add_update_arguments(update)
    update.set_defaults(func=run_update)

    recovery = subparsers.add_parser(
        "recovery",
        help="bootstrap recovery with a signed Slot-A package",
    )
    add_update_arguments(recovery)
    recovery.set_defaults(func=run_update)

    reset = subparsers.add_parser("reset", help="request a controlled target reset")
    reset.set_defaults(func=run_reset)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.retries < 0:
        parser.error("--retries must be non-negative")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        return int(args.func(args))
    except Stm32CtlError as exc:
        if args.json:
            _print_json({"result": "failed", "error": str(exc)})
        else:
            print(f"stm32ctl: error: {exc}", file=sys.stderr)
        return exc.exit_code
    except KeyboardInterrupt:
        print("stm32ctl: interrupted", file=sys.stderr)
        return EXIT_FAILED


if __name__ == "__main__":
    raise SystemExit(main())
