#!/usr/bin/env python3
"""Fail-closed Ed25519 key lifecycle helper for research builds.

The tool never prints a private seed. Existing files are never overwritten.
Public fingerprints are SHA-256 over the raw 32-byte Ed25519 public key.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import secrets
import stat
import sys
from pathlib import Path

from nacl.signing import SigningKey

SEED_SIZE = 32
PUBLIC_KEY_SIZE = 32
PURPOSES = {
    "ci-test": "TEST KEY - NOT FOR PRODUCTION",
    "developer-test": "TEST KEY - NOT FOR PRODUCTION",
    "research-device": "RESEARCH DEVICE KEY - NOT FOR PRODUCTION",
    "production-like": "PRODUCTION-LIKE OFFLINE KEY - KEEP OFFLINE",
}


def fingerprint(public_key: bytes) -> str:
    return hashlib.sha256(public_key).hexdigest()


def read_seed(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != SEED_SIZE:
        raise ValueError(f"seed must be exactly {SEED_SIZE} bytes")
    mode = stat.S_IMODE(path.stat().st_mode)
    if mode & 0o077:
        raise ValueError(f"private seed permissions must be 0600: {path}")
    return data


def write_new(path: Path, data: bytes, mode: int) -> None:
    if path.exists():
        raise FileExistsError(f"refusing to overwrite existing file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    try:
        with os.fdopen(fd, "wb") as stream:
            fd = -1
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        if fd >= 0:
            os.close(fd)
    os.chmod(path, mode)


def public_key_from_source(args: argparse.Namespace) -> bytes:
    if (args.seed is None) == (args.public_key_hex is None):
        raise ValueError("provide exactly one of --seed or --public-key-hex")
    if args.seed is not None:
        return bytes(SigningKey(read_seed(args.seed)).verify_key)
    try:
        value = bytes.fromhex(args.public_key_hex)
    except ValueError as exc:
        raise ValueError("public key must be hexadecimal") from exc
    if len(value) != PUBLIC_KEY_SIZE:
        raise ValueError("public key must be exactly 32 bytes")
    return value


def command_generate(args: argparse.Namespace) -> int:
    seed = secrets.token_bytes(SEED_SIZE)
    public_key = bytes(SigningKey(seed).verify_key)
    write_new(args.seed, seed, 0o600)
    header = (
        f"/* {PURPOSES[args.purpose]} */\n"
        f"/* Raw public-key fingerprint (SHA-256): {fingerprint(public_key)} */\n"
        "static const unsigned char firmware_public_key[32] = {\n    "
        + ", ".join(f"0x{byte:02X}" for byte in public_key)
        + "\n};\n"
    ).encode("ascii")
    try:
        write_new(args.public_header, header, 0o644)
    except Exception:
        args.seed.unlink(missing_ok=True)
        raise
    print(f"purpose={args.purpose}")
    print(f"public_key_sha256={fingerprint(public_key)}")
    return 0


def command_inspect(args: argparse.Namespace) -> int:
    public_key = public_key_from_source(args)
    print(f"public_key_sha256={fingerprint(public_key)}")
    print(f"public_key_hex={public_key.hex()}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate")
    generate.add_argument("--purpose", choices=sorted(PURPOSES), required=True)
    generate.add_argument("--seed", type=Path, required=True)
    generate.add_argument("--public-header", type=Path, required=True)
    generate.set_defaults(function=command_generate)
    inspect = subparsers.add_parser("inspect")
    inspect.add_argument("--seed", type=Path)
    inspect.add_argument("--public-key-hex")
    inspect.set_defaults(function=command_inspect)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return int(args.function(args))
    except (OSError, ValueError, FileExistsError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
