#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

from module_format import parse_package


def _load_public_key(value: str) -> bytes:
    path = Path(value)
    if path.exists():
        data = path.read_bytes()
        if len(data) == 32:
            return data
        value = data.decode("ascii").strip()
    key = bytes.fromhex(value)
    if len(key) != 32:
        raise argparse.ArgumentTypeError("public key must be 32 bytes")
    return key


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect an EXP067 module package")
    parser.add_argument("package", type=Path)
    parser.add_argument("--public-key", type=_load_public_key)
    args = parser.parse_args()

    manifest = parse_package(args.package.read_bytes(), args.public_key)
    header = manifest.header
    print(f"module_id=0x{manifest.module_id:08X}")
    print(f"module_version={manifest.module_version}")
    print(f"module_type={header.module_type}")
    print(f"min_platform={header.min_platform}")
    print(f"abi_version={header.abi_version}")
    print(f"payload_size={header.payload_size}")
    print(f"capabilities={','.join(f'0x{cap:08X}' for cap in manifest.capabilities)}")
    print(f"signature_verified={'yes' if args.public_key is not None else 'not requested'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
