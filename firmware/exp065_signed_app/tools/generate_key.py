#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
from pathlib import Path

from nacl.signing import SigningKey


def format_public_key_header(public_key: bytes) -> str:
    rows = []

    for offset in range(0, len(public_key), 8):
        values = ", ".join(
            f"0x{value:02X}U" for value in public_key[offset:offset + 8]
        )
        rows.append(f"    {values},")

    body = "\n".join(rows)

    return f"""#ifndef FIRMWARE_PUBLIC_KEY_H
#define FIRMWARE_PUBLIC_KEY_H

#include <stdint.h>

static const uint8_t firmware_public_key[32] = {{
{body}
}};

#endif
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace an existing private seed.",
    )
    args = parser.parse_args()

    args.seed.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)

    if args.seed.exists() and not args.force:
        seed = args.seed.read_bytes()

        if len(seed) != 32:
            raise SystemExit(
                f"Existing seed has invalid length: {len(seed)} bytes"
            )

        signing_key = SigningKey(seed)
        print(f"Using existing seed: {args.seed}")
    else:
        signing_key = SigningKey.generate()

        file_descriptor = os.open(
            args.seed,
            os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
            0o600,
        )

        with os.fdopen(file_descriptor, "wb") as seed_file:
            seed_file.write(bytes(signing_key))

        print(f"Created private seed: {args.seed}")

    os.chmod(args.seed, 0o600)

    public_key = bytes(signing_key.verify_key)
    args.header.write_text(
        format_public_key_header(public_key),
        encoding="ascii",
    )

    print(f"Public key: {public_key.hex()}")
    print(f"Header: {args.header}")


if __name__ == "__main__":
    main()
