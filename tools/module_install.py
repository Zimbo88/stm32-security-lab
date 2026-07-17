#!/usr/bin/env python3
"""EXP070 atomic A/B module installation and rollback simulator."""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import struct
import zlib
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path

from bytecode_vm import CAP_OUTPUT, CAP_RCC_READ
from module_format import TYPE_BYTECODE, TYPE_NATIVE, Manifest, parse_package

INSTALL_RECORD_SIZE = 128
INSTALL_RECORD_BODY_FORMAT = "<4sHH10I16s32s"
INSTALL_RECORD_BODY_SIZE = struct.calcsize(INSTALL_RECORD_BODY_FORMAT)
assert INSTALL_RECORD_BODY_SIZE == 96

INSTALL_RECORD_MAGIC = b"IC70"
INSTALL_RECORD_VERSION = 1
INSTALL_RECORD_KIND_STATE = 1
INSTALL_RECORD_KIND_REVOCATION = 2

SLOT_A = 0
SLOT_B = 1
SLOT_NONE = 0xFFFFFFFF

SUPPORTED_INSTALL_CAPABILITIES = frozenset({CAP_RCC_READ, CAP_OUTPUT})
SUPPORTED_MODULE_TYPES = frozenset({TYPE_BYTECODE, TYPE_NATIVE})


class InstallError(ValueError):
    pass


class PowerLoss(RuntimeError):
    pass


class InstallState(IntEnum):
    EMPTY = 0
    RECEIVING = 1
    VERIFIED = 2
    PENDING = 3
    ACTIVE = 4
    CONFIRMED = 5
    REJECTED = 6
    QUARANTINED = 7


@dataclass(frozen=True)
class CatalogRecord:
    kind: int
    sequence: int
    module_id: int
    module_version: int
    min_version: int
    state: InstallState
    active_slot: int
    candidate_slot: int
    failures: int
    result: int
    signer_key_id: bytes
    fingerprint: bytes


@dataclass(frozen=True)
class VerifiedPackage:
    module_id: int
    module_version: int
    module_type: int
    signer_key_id: bytes
    fingerprint: bytes
    capabilities: tuple[int, ...]
    manifest: Manifest


@dataclass(frozen=True)
class RecoveryView:
    records: tuple[CatalogRecord, ...]
    latest_by_module: dict[int, CatalogRecord]
    confirmed_by_module: dict[int, CatalogRecord]
    min_version_by_module: dict[int, int]
    revoked_signers: frozenset[bytes]


def _u32(name: str, value: int, *, allow_none: bool = False) -> int:
    if allow_none and value == SLOT_NONE:
        return value
    if not isinstance(value, int) or value < 0 or value > 0xFFFFFFFF:
        raise InstallError(f"{name} must fit uint32")
    return value


def _slot(name: str, value: int) -> int:
    if value not in (SLOT_A, SLOT_B, SLOT_NONE):
        raise InstallError(f"invalid {name}")
    return value


def _state(value: int) -> InstallState:
    try:
        return InstallState(value)
    except ValueError as exc:
        raise InstallError("invalid install state") from exc


def encode_record(record: CatalogRecord) -> bytes:
    if record.kind not in (INSTALL_RECORD_KIND_STATE, INSTALL_RECORD_KIND_REVOCATION):
        raise InstallError("invalid catalog record kind")
    if len(record.signer_key_id) != 16:
        raise InstallError("invalid signer key id")
    if len(record.fingerprint) != 32:
        raise InstallError("invalid fingerprint")
    if record.kind == INSTALL_RECORD_KIND_STATE:
        _u32("module_id", record.module_id)
        if record.module_id == 0 or record.module_version == 0:
            raise InstallError("invalid module identity")
        _slot("active_slot", record.active_slot)
        _slot("candidate_slot", record.candidate_slot)
    if record.kind == INSTALL_RECORD_KIND_REVOCATION:
        if record.signer_key_id == b"\0" * 16:
            raise InstallError("invalid revocation signer")
        if (
            record.module_id != 0
            or record.module_version != 0
            or record.min_version != 0
            or record.state != InstallState.EMPTY
            or record.active_slot != SLOT_NONE
            or record.candidate_slot != SLOT_NONE
            or record.failures != 0
            or record.result != 0
            or record.fingerprint != b"\0" * 32
        ):
            raise InstallError("invalid revocation record")

    body = struct.pack(
        INSTALL_RECORD_BODY_FORMAT,
        INSTALL_RECORD_MAGIC,
        INSTALL_RECORD_VERSION,
        record.kind,
        _u32("sequence", record.sequence),
        _u32("module_id", record.module_id),
        _u32("module_version", record.module_version),
        _u32("min_version", record.min_version),
        _u32("state", int(record.state)),
        _u32("active_slot", record.active_slot, allow_none=True),
        _u32("candidate_slot", record.candidate_slot, allow_none=True),
        _u32("failures", record.failures),
        _u32("result", record.result),
        0,
        record.signer_key_id,
        record.fingerprint,
    )
    crc = zlib.crc32(body) & 0xFFFFFFFF
    prefix = body + struct.pack("<I", crc)
    return prefix + b"\0" * (INSTALL_RECORD_SIZE - len(prefix))


def decode_record(raw: bytes) -> CatalogRecord:
    raw = bytes(raw)
    if len(raw) != INSTALL_RECORD_SIZE:
        raise InstallError("catalog record size")
    if raw[INSTALL_RECORD_BODY_SIZE + 4 :] != b"\0" * (INSTALL_RECORD_SIZE - INSTALL_RECORD_BODY_SIZE - 4):
        raise InstallError("catalog record reserved bytes")
    body = raw[:INSTALL_RECORD_BODY_SIZE]
    expected_crc = struct.unpack_from("<I", raw, INSTALL_RECORD_BODY_SIZE)[0]
    if zlib.crc32(body) & 0xFFFFFFFF != expected_crc:
        raise InstallError("catalog record crc")
    (
        magic,
        version,
        kind,
        sequence,
        module_id,
        module_version,
        min_version,
        state_value,
        active_slot,
        candidate_slot,
        failures,
        result,
        reserved0,
        signer_key_id,
        fingerprint,
    ) = struct.unpack(INSTALL_RECORD_BODY_FORMAT, body)
    if magic != INSTALL_RECORD_MAGIC or version != INSTALL_RECORD_VERSION or reserved0 != 0:
        raise InstallError("catalog record identity")
    state = _state(state_value)
    record = CatalogRecord(
        kind,
        sequence,
        module_id,
        module_version,
        min_version,
        state,
        _slot("active_slot", active_slot),
        _slot("candidate_slot", candidate_slot),
        failures,
        result,
        signer_key_id,
        fingerprint,
    )
    if kind == INSTALL_RECORD_KIND_STATE:
        if module_id == 0 or module_version == 0:
            raise InstallError("catalog module identity")
    elif kind == INSTALL_RECORD_KIND_REVOCATION:
        if (
            module_id != 0
            or module_version != 0
            or min_version != 0
            or state != InstallState.EMPTY
            or active_slot != SLOT_NONE
            or candidate_slot != SLOT_NONE
            or failures != 0
            or result != 0
            or signer_key_id == b"\0" * 16
            or fingerprint != b"\0" * 32
        ):
            raise InstallError("catalog revocation identity")
    else:
        raise InstallError("catalog record kind")
    return record


def iter_valid_records(catalog: bytes) -> Iterable[CatalogRecord]:
    catalog = bytes(catalog)
    complete = len(catalog) - (len(catalog) % INSTALL_RECORD_SIZE)
    for offset in range(0, complete, INSTALL_RECORD_SIZE):
        try:
            yield decode_record(catalog[offset : offset + INSTALL_RECORD_SIZE])
        except InstallError:
            continue


def recover_catalog(catalog: bytes) -> RecoveryView:
    records = tuple(iter_valid_records(catalog))
    latest: dict[int, CatalogRecord] = {}
    confirmed: dict[int, CatalogRecord] = {}
    min_versions: dict[int, int] = {}
    revoked: set[bytes] = set()

    for record in records:
        if record.kind == INSTALL_RECORD_KIND_REVOCATION:
            revoked.add(record.signer_key_id)
            continue
        min_versions[record.module_id] = max(min_versions.get(record.module_id, 0), record.min_version)
        if record.module_version != 0:
            min_versions[record.module_id] = max(min_versions.get(record.module_id, 0), record.min_version)
        if record.module_id not in latest or record.sequence >= latest[record.module_id].sequence:
            latest[record.module_id] = record
        if record.state == InstallState.CONFIRMED:
            if record.module_id not in confirmed or record.sequence >= confirmed[record.module_id].sequence:
                confirmed[record.module_id] = record

    return RecoveryView(records, latest, confirmed, min_versions, frozenset(revoked))


class SimulatedFlash:
    def __init__(self) -> None:
        self.catalog = bytearray()
        self.slots: dict[str, bytes] = {}
        self.fail_after: int | None = None
        self.write_count = 0

    def clone(self) -> "SimulatedFlash":
        other = SimulatedFlash()
        other.catalog = bytearray(self.catalog)
        other.slots = dict(self.slots)
        return other

    def set_power_loss_after(self, writes: int | None) -> None:
        self.fail_after = writes
        self.write_count = 0

    def append_catalog(self, record: bytes) -> None:
        self.catalog.extend(record)
        self._after_persistent_write()

    def append_partial_catalog(self, record: bytes, length: int) -> None:
        self.catalog.extend(record[:length])

    def write_slot(self, module_id: int, slot: int, package: bytes) -> None:
        self.slots[self._slot_key(module_id, slot)] = bytes(package)
        self._after_persistent_write()

    def write_partial_slot(self, module_id: int, slot: int, package: bytes, length: int) -> None:
        self.slots[self._slot_key(module_id, slot)] = bytes(package[:length])

    def erase_slot(self, module_id: int, slot: int) -> None:
        self.slots.pop(self._slot_key(module_id, slot), None)
        self._after_persistent_write()

    def read_slot(self, module_id: int, slot: int) -> bytes | None:
        return self.slots.get(self._slot_key(module_id, slot))

    def to_json(self) -> str:
        return json.dumps(
            {
                "catalog": base64.b64encode(bytes(self.catalog)).decode("ascii"),
                "slots": {key: base64.b64encode(value).decode("ascii") for key, value in self.slots.items()},
            },
            sort_keys=True,
        )

    @classmethod
    def from_json(cls, text: str) -> "SimulatedFlash":
        flash = cls()
        if not text.strip():
            return flash
        data = json.loads(text)
        flash.catalog = bytearray(base64.b64decode(data.get("catalog", "")))
        flash.slots = {
            key: base64.b64decode(value)
            for key, value in data.get("slots", {}).items()
        }
        return flash

    @staticmethod
    def _slot_key(module_id: int, slot: int) -> str:
        if slot not in (SLOT_A, SLOT_B):
            raise InstallError("invalid module slot")
        return f"{module_id:08X}:{slot}"

    def _after_persistent_write(self) -> None:
        self.write_count += 1
        if self.fail_after is not None and self.write_count == self.fail_after:
            raise PowerLoss(f"simulated power loss after write {self.write_count}")


class ModuleInstaller:
    def __init__(
        self,
        flash: SimulatedFlash,
        *,
        module_public_key: bytes,
        firmware_public_key: bytes | None = None,
        platform_version: int = 1,
        abi_version: int = 1,
        failure_threshold: int = 2,
    ) -> None:
        if len(module_public_key) != 32:
            raise ValueError("module public key must be 32 bytes")
        if firmware_public_key is not None and len(firmware_public_key) != 32:
            raise ValueError("firmware public key must be 32 bytes")
        self.flash = flash
        self.module_public_key = bytes(module_public_key)
        self.module_signer_key_id = hashlib.sha256(self.module_public_key).digest()[:16]
        self.firmware_key_id = hashlib.sha256(firmware_public_key).digest()[:16] if firmware_public_key else None
        self.platform_version = platform_version
        self.abi_version = abi_version
        self.failure_threshold = failure_threshold

    def list(self) -> list[CatalogRecord]:
        return sorted(self.recover().latest_by_module.values(), key=lambda record: record.module_id)

    def inspect(self, module_id: int) -> CatalogRecord | None:
        return self.recover().latest_by_module.get(module_id)

    def recover(self) -> RecoveryView:
        return recover_catalog(bytes(self.flash.catalog))

    def verify_package(self, package: bytes, *, expected_module_id: int | None = None) -> VerifiedPackage:
        view = self.recover()
        if self.module_signer_key_id in view.revoked_signers:
            raise InstallError("module signer revoked")
        if self.firmware_key_id is not None and hmac.compare_digest(self.module_signer_key_id, self.firmware_key_id):
            raise InstallError("module signer must be separate from firmware signer")
        manifest = parse_package(
            package,
            self.module_public_key,
            min_platform=self.platform_version,
            abi_version=self.abi_version,
            min_module_version=0,
            supported_capabilities=SUPPORTED_INSTALL_CAPABILITIES,
        )
        if manifest.header.module_type not in SUPPORTED_MODULE_TYPES:
            raise InstallError("unsupported module type")
        if expected_module_id is not None and manifest.module_id != expected_module_id:
            raise InstallError("module id mismatch")
        min_version = view.min_version_by_module.get(manifest.module_id, 0)
        if manifest.module_version < min_version:
            raise InstallError("rollback version")
        signer_key_id = hashlib.sha256(self.module_public_key).digest()[:16]
        return VerifiedPackage(
            manifest.module_id,
            manifest.module_version,
            manifest.header.module_type,
            signer_key_id,
            hashlib.sha256(package).digest(),
            manifest.capabilities,
            manifest,
        )

    def install(self, package: bytes) -> VerifiedPackage:
        verified = self.verify_package(package)
        view = self.recover()
        current = view.latest_by_module.get(verified.module_id)
        if current is not None and current.state in (InstallState.PENDING, InstallState.ACTIVE):
            raise InstallError("active module installation in progress")
        confirmed = view.confirmed_by_module.get(verified.module_id)
        active_slot = confirmed.active_slot if confirmed else SLOT_NONE
        candidate_slot = SLOT_A if active_slot in (SLOT_NONE, SLOT_B) else SLOT_B
        min_version = view.min_version_by_module.get(verified.module_id, 0)
        if min_version != 0 and verified.module_version <= min_version:
            raise InstallError("replay or rollback version")

        self._append_state(verified, InstallState.RECEIVING, active_slot, candidate_slot, min_version)
        self.flash.write_slot(verified.module_id, candidate_slot, package)
        self._verify_slot(verified.module_id, candidate_slot)
        self._append_state(verified, InstallState.VERIFIED, active_slot, candidate_slot, min_version)
        return verified

    def verify(self, module_id: int) -> VerifiedPackage:
        record = self._require_latest(module_id)
        slot = record.candidate_slot if record.candidate_slot != SLOT_NONE else record.active_slot
        if slot == SLOT_NONE:
            raise InstallError("no package slot to verify")
        return self._verify_slot(module_id, slot)

    def activate(self, module_id: int) -> None:
        record = self._require_latest(module_id)
        if record.state not in (InstallState.VERIFIED, InstallState.REJECTED):
            raise InstallError("module is not verified")
        package = self._read_slot_or_fail(module_id, record.candidate_slot)
        verified = self.verify_package(package, expected_module_id=module_id)
        self._append_state(
            verified,
            InstallState.PENDING,
            record.active_slot,
            record.candidate_slot,
            record.min_version,
            failures=record.failures,
        )
        self._append_state(
            verified,
            InstallState.ACTIVE,
            record.candidate_slot,
            record.active_slot,
            record.min_version,
            failures=record.failures,
        )

    def confirm(self, module_id: int) -> None:
        record = self._require_latest(module_id)
        if record.state != InstallState.ACTIVE:
            raise InstallError("module is not active")
        package = self._read_slot_or_fail(module_id, record.active_slot)
        verified = self.verify_package(package, expected_module_id=module_id)
        min_version = max(record.min_version, verified.module_version)
        self._append_state(verified, InstallState.CONFIRMED, record.active_slot, SLOT_NONE, min_version)

    def rollback(self, module_id: int) -> None:
        record = self._require_latest(module_id)
        confirmed = self.recover().confirmed_by_module.get(module_id)
        if confirmed is None:
            raise InstallError("no confirmed module to roll back to")
        package = self._read_slot_or_fail(module_id, confirmed.active_slot)
        verified = self.verify_package(package, expected_module_id=module_id)
        self._append_state(
            verified,
            InstallState.REJECTED,
            confirmed.active_slot,
            record.active_slot if record.active_slot != confirmed.active_slot else record.candidate_slot,
            max(record.min_version, confirmed.min_version),
            failures=record.failures + 1,
        )

    def record_initialization_result(self, module_id: int, *, success: bool) -> None:
        record = self._require_latest(module_id)
        if record.state not in (InstallState.PENDING, InstallState.ACTIVE):
            raise InstallError("module is not pending or active")
        if success:
            return
        confirmed = self.recover().confirmed_by_module.get(module_id)
        active_slot = confirmed.active_slot if confirmed else SLOT_NONE
        failing_slot = record.active_slot if record.state == InstallState.ACTIVE else record.candidate_slot
        package = self._read_slot_or_fail(module_id, failing_slot)
        verified = self.verify_package(package, expected_module_id=module_id)
        failures = record.failures + 1
        state = InstallState.QUARANTINED if failures >= self.failure_threshold else InstallState.REJECTED
        self._append_state(verified, state, active_slot, failing_slot, record.min_version, failures=failures)

    def quarantine(self, module_id: int) -> None:
        record = self._require_latest(module_id)
        slot = record.candidate_slot if record.candidate_slot != SLOT_NONE else record.active_slot
        package = self._read_slot_or_fail(module_id, slot)
        verified = self.verify_package(package, expected_module_id=module_id)
        confirmed = self.recover().confirmed_by_module.get(module_id)
        active_slot = confirmed.active_slot if confirmed else SLOT_NONE
        self._append_state(verified, InstallState.QUARANTINED, active_slot, slot, record.min_version, failures=record.failures)

    def remove_candidate(self, module_id: int) -> None:
        record = self._require_latest(module_id)
        candidate = record.candidate_slot
        if candidate == SLOT_NONE:
            return
        self.flash.erase_slot(module_id, candidate)
        confirmed = self.recover().confirmed_by_module.get(module_id)
        if confirmed:
            package = self._read_slot_or_fail(module_id, confirmed.active_slot)
            verified = self.verify_package(package, expected_module_id=module_id)
            self._append_state(verified, InstallState.CONFIRMED, confirmed.active_slot, SLOT_NONE, record.min_version)
        else:
            empty = CatalogRecord(
                INSTALL_RECORD_KIND_STATE,
                self._next_sequence(),
                module_id,
                record.module_version,
                record.min_version,
                InstallState.EMPTY,
                SLOT_NONE,
                SLOT_NONE,
                record.failures,
                0,
                record.signer_key_id,
                record.fingerprint,
            )
            self.flash.append_catalog(encode_record(empty))

    def revoke_signer(self, signer_key_id: bytes) -> None:
        if len(signer_key_id) != 16:
            raise InstallError("signer key id length")
        record = CatalogRecord(
            INSTALL_RECORD_KIND_REVOCATION,
            self._next_sequence(),
            0,
            0,
            0,
            InstallState.EMPTY,
            SLOT_NONE,
            SLOT_NONE,
            0,
            0,
            signer_key_id,
            b"\0" * 32,
        )
        self.flash.append_catalog(encode_record(record))

    def current_confirmed_package(self, module_id: int) -> bytes | None:
        confirmed = self.recover().confirmed_by_module.get(module_id)
        if confirmed is None:
            return None
        package = self.flash.read_slot(module_id, confirmed.active_slot)
        if package is None or hashlib.sha256(package).digest() != confirmed.fingerprint:
            return None
        return package

    def _verify_slot(self, module_id: int, slot: int) -> VerifiedPackage:
        package = self._read_slot_or_fail(module_id, slot)
        return self.verify_package(package, expected_module_id=module_id)

    def _read_slot_or_fail(self, module_id: int, slot: int) -> bytes:
        if slot == SLOT_NONE:
            raise InstallError("missing module slot")
        package = self.flash.read_slot(module_id, slot)
        if package is None:
            raise InstallError("missing module package")
        return package

    def _require_latest(self, module_id: int) -> CatalogRecord:
        record = self.recover().latest_by_module.get(module_id)
        if record is None:
            raise InstallError("unknown module")
        return record

    def _append_state(
        self,
        verified: VerifiedPackage,
        state: InstallState,
        active_slot: int,
        candidate_slot: int,
        min_version: int,
        *,
        failures: int = 0,
        result: int = 0,
    ) -> None:
        record = CatalogRecord(
            INSTALL_RECORD_KIND_STATE,
            self._next_sequence(),
            verified.module_id,
            verified.module_version,
            min_version,
            state,
            active_slot,
            candidate_slot,
            failures,
            result,
            verified.signer_key_id,
            verified.fingerprint,
        )
        self.flash.append_catalog(encode_record(record))

    def _next_sequence(self) -> int:
        records = self.recover().records
        return 1 if not records else max(record.sequence for record in records) + 1


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


def _u32_arg(text: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if value < 0 or value > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("value must fit uint32")
    return value


def _load_flash(path: Path) -> SimulatedFlash:
    return SimulatedFlash.from_json(path.read_text()) if path.exists() else SimulatedFlash()


def _save_flash(path: Path, flash: SimulatedFlash) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(flash.to_json())


def _installer(args: argparse.Namespace, flash: SimulatedFlash) -> ModuleInstaller:
    return ModuleInstaller(
        flash,
        module_public_key=args.module_public_key,
        firmware_public_key=args.firmware_public_key,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="EXP070 module installation simulator")
    parser.add_argument("--flash", type=Path, required=True)
    parser.add_argument("--module-public-key", type=_load_public_key, required=True)
    parser.add_argument("--firmware-public-key", type=_load_public_key)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("list")
    inspect_p = sub.add_parser("inspect")
    inspect_p.add_argument("module_id", type=_u32_arg)
    install_p = sub.add_parser("install")
    install_p.add_argument("package", type=Path)
    verify_p = sub.add_parser("verify")
    verify_p.add_argument("module_id", type=_u32_arg)
    activate_p = sub.add_parser("activate")
    activate_p.add_argument("module_id", type=_u32_arg)
    confirm_p = sub.add_parser("confirm")
    confirm_p.add_argument("module_id", type=_u32_arg)
    rollback_p = sub.add_parser("rollback")
    rollback_p.add_argument("module_id", type=_u32_arg)
    quarantine_p = sub.add_parser("quarantine")
    quarantine_p.add_argument("module_id", type=_u32_arg)
    remove_p = sub.add_parser("remove-candidate")
    remove_p.add_argument("module_id", type=_u32_arg)
    revoke_p = sub.add_parser("revoke-signer")
    revoke_p.add_argument("signer_key_id_hex")
    sub.add_parser("catalog-recovery")

    args = parser.parse_args(argv)
    flash = _load_flash(args.flash)
    installer = _installer(args, flash)

    if args.command == "list":
        for record in installer.list():
            print(f"0x{record.module_id:08X} {record.state.name} v{record.module_version}")
    elif args.command == "inspect":
        record = installer.inspect(args.module_id)
        print("none" if record is None else f"0x{record.module_id:08X} {record.state.name} v{record.module_version}")
    elif args.command == "install":
        verified = installer.install(args.package.read_bytes())
        print(f"installed candidate 0x{verified.module_id:08X} v{verified.module_version}")
    elif args.command == "verify":
        verified = installer.verify(args.module_id)
        print(f"verified 0x{verified.module_id:08X} v{verified.module_version}")
    elif args.command == "activate":
        installer.activate(args.module_id)
        print("activated pending")
    elif args.command == "confirm":
        installer.confirm(args.module_id)
        print("confirmed")
    elif args.command == "rollback":
        installer.rollback(args.module_id)
        print("rolled back")
    elif args.command == "quarantine":
        installer.quarantine(args.module_id)
        print("quarantined")
    elif args.command == "remove-candidate":
        installer.remove_candidate(args.module_id)
        print("candidate removed")
    elif args.command == "revoke-signer":
        installer.revoke_signer(bytes.fromhex(args.signer_key_id_hex))
        print("signer revoked")
    elif args.command == "catalog-recovery":
        view = installer.recover()
        print(f"records={len(view.records)} modules={len(view.latest_by_module)} revoked={len(view.revoked_signers)}")
    else:
        raise AssertionError(args.command)

    _save_flash(args.flash, flash)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
