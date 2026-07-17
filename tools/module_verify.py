#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from module_format import parse_package


def _u32(text: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if value < 0 or value > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("value must fit uint32")
    return value


def _load_public_key(value: str) -> bytes:
    path = Path(value)
    if path.exists():
        data = path.read_bytes()
        if len(data) == 32:
            return data
        value = data.decode("ascii").strip()
    try:
        key = bytes.fromhex(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("public key must be 32 raw bytes or hex") from exc
    if len(key) != 32:
        raise argparse.ArgumentTypeError("public key must be 32 bytes")
    return key


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify an EXP067 module package")
    parser.add_argument("package", type=Path)
    parser.add_argument("--public-key", required=True, type=_load_public_key)
    parser.add_argument("--min-platform", type=_u32, default=1)
    parser.add_argument("--abi-version", type=_u32, default=1)
    parser.add_argument("--min-module-version", type=_u32, default=0)
    parser.add_argument("--supported-cap", type=_u32, action="append", default=None)
    args = parser.parse_args()

    manifest = parse_package(
        args.package.read_bytes(),
        args.public_key,
        min_platform=args.min_platform,
        abi_version=args.abi_version,
        min_module_version=args.min_module_version,
        supported_capabilities=None if args.supported_cap is None else set(args.supported_cap),
    )
    print(
        f"valid module 0x{manifest.module_id:08X} "
        f"version {manifest.module_version} "
        f"capabilities {len(manifest.capabilities)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
