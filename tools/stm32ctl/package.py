from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .errors import PackageValidationError

TOOLS_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = TOOLS_ROOT.parent
DEFAULT_PUBLIC_KEY_HEADER = (
    REPO_ROOT
    / "firmware"
    / "exp045_bootloader_v2"
    / "src"
    / "firmware_public_key.h"
)

if str(TOOLS_ROOT) not in sys.path:
    sys.path.insert(0, str(TOOLS_ROOT))

import release_artifacts  # noqa: E402
import update_package  # noqa: E402


@dataclass(frozen=True)
class PackageInfo:
    path: Path
    data: bytes
    slot: str
    image_version: int
    payload_size: int
    package_size: int
    signed_header_size: int
    vector_address: int
    payload_sha512: str
    report: dict[str, Any]


def _load_public_key(public_key_hex: str | None, public_key_header: Path | None) -> bytes:
    if public_key_hex is not None and public_key_header is not None:
        raise PackageValidationError("provide only one public key source")

    if public_key_hex is not None:
        try:
            key = bytes.fromhex(public_key_hex)
        except ValueError as exc:
            raise PackageValidationError("public key hex is not valid hexadecimal") from exc
        if len(key) != 32:
            raise PackageValidationError(f"public key must be 32 bytes, got {len(key)}")
        return key

    header = public_key_header if public_key_header is not None else DEFAULT_PUBLIC_KEY_HEADER
    try:
        return release_artifacts.parse_public_key_header(header)
    except Exception as exc:
        raise PackageValidationError(str(exc)) from exc


def load_and_verify_package(
    path: Path,
    *,
    public_key_hex: str | None = None,
    public_key_header: Path | None = None,
) -> PackageInfo:
    try:
        data = update_package.read_file(path, "update package")
        inspected = update_package.inspect_package_bytes(data)
        public_key = _load_public_key(public_key_hex, public_key_header)
        verified = update_package.verify_package_bytes(
            data,
            public_key,
            slot=str(inspected["slot"]),
        )
    except Exception as exc:
        raise PackageValidationError(str(exc)) from exc

    package = verified["package"]
    return PackageInfo(
        path=path,
        data=data,
        slot=str(package["slot"]),
        image_version=int(package["image_version"]),
        payload_size=int(package["payload_size"]),
        package_size=int(package["package_size"]),
        signed_header_size=int(package["signed_header_size"]),
        vector_address=int(package["vector_address"]),
        payload_sha512=str(package["payload_sha512"]),
        report=verified,
    )
