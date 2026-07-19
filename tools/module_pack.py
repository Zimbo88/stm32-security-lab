#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from module_format import TYPE_BYTECODE, TYPE_NATIVE, build_package, parse_package
from nacl.signing import SigningKey


def _u32(text: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if value < 0 or value > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("value must fit uint32")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Pack and sign an EXP067 module")
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--seed", type=Path, required=True)
    parser.add_argument("--id", type=_u32, required=True)
    parser.add_argument("--version", type=_u32, required=True)
    parser.add_argument("--cap", type=_u32, action="append", default=[])
    parser.add_argument(
        "--module-type",
        choices=("bytecode", "native"),
        default="bytecode",
    )
    parser.add_argument("--min-platform", type=_u32, default=1)
    parser.add_argument("--abi-version", type=_u32, default=1)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    seed = args.seed.read_bytes()
    module_type = TYPE_BYTECODE if args.module_type == "bytecode" else TYPE_NATIVE
    package = build_package(
        args.payload.read_bytes(),
        module_id=args.id,
        version=args.version,
        seed=seed,
        capabilities=args.cap,
        module_type=module_type,
        min_platform=args.min_platform,
        abi_version=args.abi_version,
    )
    parse_package(
        package,
        bytes(SigningKey(seed).verify_key),
        min_platform=args.min_platform,
        abi_version=args.abi_version,
        min_module_version=args.version,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(f"wrote {len(package)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
