"""EXP069 constrained native module validator and lifecycle simulator.

The loader accepts only signed EXP067 ``TYPE_NATIVE`` packages.  It validates a
small custom position-independent module payload and simulates module lifecycle
without executing host-native or ARM code.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import struct
from collections.abc import Container, Sequence
from dataclasses import dataclass, replace
from enum import IntEnum
from pathlib import Path

from bytecode_vm import CAP_OUTPUT, CAP_RCC_READ, RCC_REGISTER_NAMES
from module_format import TYPE_NATIVE, parse_package

NATIVE_MAGIC = b"NMV1"
NATIVE_VERSION = 1
NATIVE_HEADER_FORMAT = "<4sHH13I16s"
NATIVE_HEADER_SIZE = struct.calcsize(NATIVE_HEADER_FORMAT)
assert NATIVE_HEADER_SIZE == 76

PLATFORM_VERSION = 1
PLATFORM_ABI_VERSION = 1
PLATFORM_API_VERSION = 1

MAX_CODE_SIZE = 4096
MAX_RODATA_SIZE = 2048
MAX_DATA_SIZE = 1024
MAX_BSS_SIZE = 1024
MAX_STACK_SIZE = 1024
MAX_TOTAL_RAM = 1024
MAX_TOTAL_PAYLOAD = MAX_CODE_SIZE + MAX_RODATA_SIZE + MAX_DATA_SIZE + NATIVE_HEADER_SIZE

SIM_NONE = 0
SIM_RCC_ANALYSIS = 1

SUPPORTED_NATIVE_CAPABILITIES = frozenset({CAP_RCC_READ, CAP_OUTPUT})

API_READ_RCC = 1
API_EMIT = 2


class NativeLoaderError(ValueError):
    """Raised when EXP069 validation or lifecycle handling fails closed."""


class LifecycleState(IntEnum):
    VERIFIED = 1
    LOADED = 2
    INITIALIZED = 3
    RUNNING = 4
    STOPPED = 5
    QUARANTINED = 6


@dataclass(frozen=True)
class NativeHeader:
    magic: bytes
    format_version: int
    header_size: int
    total_size: int
    code_offset: int
    code_size: int
    rodata_offset: int
    rodata_size: int
    data_offset: int
    data_size: int
    bss_size: int
    stack_size: int
    entry_offset: int
    api_version: int
    simulation_id: int
    reserved0: int
    reserved: bytes


@dataclass(frozen=True)
class NativeImage:
    header: NativeHeader
    code: bytes
    rodata: bytes
    data_initial: bytes


@dataclass(frozen=True)
class NativePackage:
    module_id: int
    module_version: int
    signer_key_id: bytes
    capabilities: frozenset[int]
    package_fingerprint: bytes
    manifest_header: object
    image: NativeImage


@dataclass(frozen=True)
class LoadedModule:
    package: NativePackage
    api: "PlatformAPI"
    data: bytes
    bss: bytes
    state: LifecycleState = LifecycleState.VERIFIED
    failures: int = 0
    quarantine_reason: str = ""


@dataclass(frozen=True)
class APIEntry:
    service_id: int
    name: str
    required_capability: int
    max_args: int


@dataclass(frozen=True)
class PlatformAPI:
    version: int = PLATFORM_API_VERSION
    entries: tuple[APIEntry, ...] = (
        APIEntry(API_READ_RCC, "read_rcc", CAP_RCC_READ, 1),
        APIEntry(API_EMIT, "emit", CAP_OUTPUT, 2),
    )

    def validate(self) -> None:
        if self.version != PLATFORM_API_VERSION:
            raise NativeLoaderError("malformed API table")
        seen_ids: set[int] = set()
        seen_names: set[str] = set()
        for entry in self.entries:
            if entry.service_id <= 0 or not entry.name:
                raise NativeLoaderError("malformed API table")
            if entry.service_id in seen_ids or entry.name in seen_names:
                raise NativeLoaderError("malformed API table")
            if entry.required_capability not in SUPPORTED_NATIVE_CAPABILITIES:
                raise NativeLoaderError("malformed API table")
            if entry.max_args < 0 or entry.max_args > 4:
                raise NativeLoaderError("malformed API table")
            seen_ids.add(entry.service_id)
            seen_names.add(entry.name)
        expected = {API_READ_RCC, API_EMIT}
        if seen_ids != expected:
            raise NativeLoaderError("malformed API table")

    def require(self, service_id: int, capabilities: Container[int]) -> APIEntry:
        self.validate()
        for entry in self.entries:
            if entry.service_id == service_id:
                if entry.required_capability not in capabilities:
                    raise NativeLoaderError(f"missing capability 0x{entry.required_capability:08X}")
                return entry
        raise NativeLoaderError("unknown API service")


@dataclass
class NativeHost:
    rcc: tuple[int, int, int] = (0, 0, 0)
    outputs: list[tuple[int, int]] | None = None
    force_init_failure: bool = False
    force_run_failure: bool = False

    def __post_init__(self) -> None:
        if len(self.rcc) != len(RCC_REGISTER_NAMES):
            raise ValueError("RCC snapshot must contain CR, CFGR, CSR")
        self.rcc = tuple(int(value) & 0xFFFFFFFF for value in self.rcc)  # type: ignore[assignment]
        if self.outputs is None:
            self.outputs = []

    def read_rcc(self, index: int) -> int:
        if index < 0 or index >= len(self.rcc):
            raise NativeLoaderError("invalid RCC index")
        return self.rcc[index]

    def emit(self, channel: int, value: int) -> None:
        if self.outputs is None:
            raise NativeLoaderError("native host output unavailable")
        self.outputs.append((channel & 0xFFFFFFFF, value & 0xFFFFFFFF))


def _u32(value: int) -> int:
    if not isinstance(value, int) or value < 0 or value > 0xFFFFFFFF:
        raise NativeLoaderError("field does not fit uint32")
    return value


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _region(name: str, offset: int, size: int, total: int, *, alignment: int) -> int:
    if offset % alignment != 0:
        raise NativeLoaderError(f"unaligned {name}")
    if offset < NATIVE_HEADER_SIZE:
        raise NativeLoaderError(f"invalid {name} range")
    if offset > total or size > total - offset:
        raise NativeLoaderError(f"invalid {name} range")
    return offset + size


def pack_native_header(header: NativeHeader) -> bytes:
    return struct.pack(
        NATIVE_HEADER_FORMAT,
        header.magic,
        header.format_version,
        header.header_size,
        header.total_size,
        header.code_offset,
        header.code_size,
        header.rodata_offset,
        header.rodata_size,
        header.data_offset,
        header.data_size,
        header.bss_size,
        header.stack_size,
        header.entry_offset,
        header.api_version,
        header.simulation_id,
        header.reserved0,
        header.reserved,
    )


def unpack_native_header(blob: bytes) -> NativeHeader:
    if len(blob) < NATIVE_HEADER_SIZE:
        raise NativeLoaderError("truncated native header")
    return NativeHeader(*struct.unpack_from(NATIVE_HEADER_FORMAT, blob, 0))


def build_native_payload(
    *,
    code: bytes,
    rodata: bytes = b"",
    data: bytes = b"",
    bss_size: int = 0,
    stack_size: int = 256,
    entry_offset: int = 0,
    api_version: int = PLATFORM_API_VERSION,
    simulation_id: int = SIM_NONE,
) -> bytes:
    code = bytes(code)
    rodata = bytes(rodata)
    data = bytes(data)
    code_offset = NATIVE_HEADER_SIZE
    rodata_offset = _align4(code_offset + len(code))
    data_offset = _align4(rodata_offset + len(rodata))
    total_size = data_offset + len(data)

    header = NativeHeader(
        NATIVE_MAGIC,
        NATIVE_VERSION,
        NATIVE_HEADER_SIZE,
        total_size,
        code_offset,
        len(code),
        rodata_offset,
        len(rodata),
        data_offset,
        len(data),
        bss_size,
        stack_size,
        entry_offset,
        api_version,
        simulation_id,
        0,
        b"\0" * 16,
    )
    payload = bytearray(pack_native_header(header))
    payload.extend(code)
    payload.extend(b"\0" * (rodata_offset - len(payload)))
    payload.extend(rodata)
    payload.extend(b"\0" * (data_offset - len(payload)))
    payload.extend(data)
    if len(payload) != total_size:
        raise NativeLoaderError("internal native payload size mismatch")
    validate_native_payload(bytes(payload))
    return bytes(payload)


def build_rcc_analysis_native_payload() -> bytes:
    # Opaque Thumb bytes for a harmless PIC stub. The host simulator below is
    # authoritative for tests; host-native code is never executed.
    return build_native_payload(
        code=b"\x00\xb5\x00\xbd",
        rodata=b"EXP069 RCC analysis\0",
        data=b"",
        bss_size=64,
        stack_size=256,
        entry_offset=0,
        simulation_id=SIM_RCC_ANALYSIS,
    )


def validate_native_payload(payload: bytes) -> NativeImage:
    payload = bytes(payload)
    header = unpack_native_header(payload)
    if (
        header.magic != NATIVE_MAGIC
        or header.format_version != NATIVE_VERSION
        or header.header_size != NATIVE_HEADER_SIZE
    ):
        raise NativeLoaderError("invalid native identity")
    if header.reserved0 != 0 or header.reserved != b"\0" * 16:
        raise NativeLoaderError("reserved native field")
    if header.total_size != len(payload) or header.total_size > MAX_TOTAL_PAYLOAD:
        raise NativeLoaderError("invalid native total size")
    if header.api_version != PLATFORM_API_VERSION:
        raise NativeLoaderError("malformed API table")
    if header.simulation_id not in (SIM_NONE, SIM_RCC_ANALYSIS):
        raise NativeLoaderError("unknown native simulation")
    if header.code_offset != NATIVE_HEADER_SIZE:
        raise NativeLoaderError("malformed native offsets")

    code_end = _region("code", header.code_offset, header.code_size, header.total_size, alignment=4)
    rodata_end = _region("rodata", header.rodata_offset, header.rodata_size, header.total_size, alignment=4)
    data_end = _region("data", header.data_offset, header.data_size, header.total_size, alignment=4)
    if code_end > header.rodata_offset or rodata_end > header.data_offset:
        raise NativeLoaderError("overlapping native regions")
    if header.rodata_offset != _align4(code_end) or header.data_offset != _align4(rodata_end):
        raise NativeLoaderError("malformed native offsets")
    if data_end != header.total_size:
        raise NativeLoaderError("unexpected trailing native data")
    if payload[code_end:header.rodata_offset] != b"\0" * (header.rodata_offset - code_end):
        raise NativeLoaderError("nonzero native padding")
    if payload[rodata_end:header.data_offset] != b"\0" * (header.data_offset - rodata_end):
        raise NativeLoaderError("nonzero native padding")

    if header.code_size == 0 or header.code_size > MAX_CODE_SIZE:
        raise NativeLoaderError("invalid native code size")
    if header.code_size % 2 != 0:
        raise NativeLoaderError("unaligned native code")
    if header.rodata_size > MAX_RODATA_SIZE:
        raise NativeLoaderError("excessive rodata")
    if header.data_size > MAX_DATA_SIZE:
        raise NativeLoaderError("excessive data")
    if header.bss_size > MAX_BSS_SIZE:
        raise NativeLoaderError("excessive bss")
    if header.stack_size == 0 or header.stack_size > MAX_STACK_SIZE or header.stack_size % 8 != 0:
        raise NativeLoaderError("excessive stack")
    if header.data_size + header.bss_size + header.stack_size > MAX_TOTAL_RAM:
        raise NativeLoaderError("excessive RAM")
    if header.entry_offset % 2 != 0:
        raise NativeLoaderError("unaligned entry")
    if header.entry_offset >= header.code_size:
        raise NativeLoaderError("entry outside code")

    return NativeImage(
        header,
        payload[header.code_offset:code_end],
        payload[header.rodata_offset:rodata_end],
        payload[header.data_offset:data_end],
    )


def load_native_package(
    package: bytes,
    public_key: bytes,
    *,
    expected_signer_key_id: bytes | None = None,
    expected_module_id: int | None = None,
    min_platform: int = PLATFORM_VERSION,
    abi_version: int = PLATFORM_ABI_VERSION,
    min_module_version: int = 0,
    allowed_capabilities: Container[int] = SUPPORTED_NATIVE_CAPABILITIES,
    api: PlatformAPI | None = None,
) -> NativePackage:
    api = api if api is not None else PlatformAPI()
    api.validate()
    manifest = parse_package(
        package,
        public_key,
        min_platform=min_platform,
        abi_version=abi_version,
        min_module_version=min_module_version,
        supported_capabilities=allowed_capabilities,
    )
    if manifest.header.module_type != TYPE_NATIVE:
        raise NativeLoaderError("bytecode package rejected by native loader")
    if expected_module_id is not None and manifest.module_id != expected_module_id:
        raise NativeLoaderError("module id mismatch")
    signer_key_id = hashlib.sha256(bytes(public_key)).digest()[:16]
    if expected_signer_key_id is not None and not hmac.compare_digest(signer_key_id, expected_signer_key_id):
        raise NativeLoaderError("signer identity mismatch")

    payload_start = manifest.header.payload_offset
    payload_end = payload_start + manifest.header.payload_size
    payload = package[payload_start:payload_end]
    image = validate_native_payload(payload)
    header = image.header

    if manifest.header.code_size != header.code_size:
        raise NativeLoaderError("package code size mismatch")
    if manifest.header.rodata_size != header.rodata_size:
        raise NativeLoaderError("package rodata size mismatch")
    if manifest.header.data_size != header.data_size:
        raise NativeLoaderError("package data size mismatch")
    if manifest.header.bss_size != header.bss_size:
        raise NativeLoaderError("package bss size mismatch")
    if manifest.header.stack_size != header.stack_size:
        raise NativeLoaderError("package stack size mismatch")
    if manifest.header.entry_offset != header.entry_offset:
        raise NativeLoaderError("package entry mismatch")

    return NativePackage(
        manifest.module_id,
        manifest.module_version,
        signer_key_id,
        frozenset(manifest.capabilities),
        hashlib.sha256(package).digest(),
        manifest.header,
        image,
    )


class NativeModuleManager:
    def __init__(
        self,
        *,
        api: PlatformAPI | None = None,
        host: NativeHost | None = None,
        failure_threshold: int = 2,
    ) -> None:
        if failure_threshold <= 0:
            raise ValueError("failure threshold must be positive")
        self.api = api if api is not None else PlatformAPI()
        self.host = host if host is not None else NativeHost()
        self.failure_threshold = failure_threshold
        self.loaded: LoadedModule | None = None

    def verify(
        self,
        package: bytes,
        public_key: bytes,
        *,
        expected_signer_key_id: bytes | None = None,
        expected_module_id: int | None = None,
        min_platform: int = PLATFORM_VERSION,
        abi_version: int = PLATFORM_ABI_VERSION,
        min_module_version: int = 0,
        allowed_capabilities: Container[int] = SUPPORTED_NATIVE_CAPABILITIES,
    ) -> NativePackage:
        return load_native_package(
            package,
            public_key,
            expected_signer_key_id=expected_signer_key_id,
            expected_module_id=expected_module_id,
            min_platform=min_platform,
            abi_version=abi_version,
            min_module_version=min_module_version,
            allowed_capabilities=allowed_capabilities,
            api=self.api,
        )

    def load(self, native_package: NativePackage) -> LoadedModule:
        self.api.validate()
        loaded = LoadedModule(
            native_package,
            self.api,
            bytes(native_package.image.data_initial),
            b"\0" * native_package.image.header.bss_size,
            state=LifecycleState.LOADED,
        )
        self.loaded = loaded
        return loaded

    def initialize(self) -> LoadedModule:
        module = self._require_loaded()
        if module.state == LifecycleState.QUARANTINED:
            raise NativeLoaderError("module quarantined")
        if module.state not in (LifecycleState.LOADED, LifecycleState.STOPPED):
            raise NativeLoaderError("module is not ready to initialize")
        if self.host.force_init_failure:
            return self._failure("initialize failed")
        self.loaded = replace(module, state=LifecycleState.INITIALIZED)
        return self.loaded

    def run(self) -> LoadedModule:
        module = self._require_loaded()
        if module.state == LifecycleState.QUARANTINED:
            raise NativeLoaderError("module quarantined")
        if module.state not in (LifecycleState.INITIALIZED, LifecycleState.STOPPED):
            return self._failure("run before initialize")
        if self.host.force_run_failure:
            return self._failure("run failed")

        self.loaded = replace(module, state=LifecycleState.RUNNING)
        self._simulate(self.loaded.package)
        return self.loaded

    def stop(self) -> LoadedModule:
        module = self._require_loaded()
        if module.state == LifecycleState.QUARANTINED:
            return module
        self.loaded = replace(module, state=LifecycleState.STOPPED)
        return self.loaded

    def quarantine(self, reason: str) -> LoadedModule:
        module = self._require_loaded()
        self.loaded = replace(
            module,
            state=LifecycleState.QUARANTINED,
            quarantine_reason=reason,
        )
        return self.loaded

    def _simulate(self, package: NativePackage) -> None:
        if package.image.header.simulation_id == SIM_NONE:
            return
        if package.image.header.simulation_id == SIM_RCC_ANALYSIS:
            self.api.require(API_READ_RCC, package.capabilities)
            self.api.require(API_EMIT, package.capabilities)
            rcc_cr = self.host.read_rcc(0)
            rcc_cfgr = self.host.read_rcc(1)
            rcc_csr = self.host.read_rcc(2)
            self.host.emit(1, rcc_cr)
            self.host.emit(2, 1 if (rcc_cr & 0x2) != 0 else 0)
            self.host.emit(3, 1 if (rcc_cr & (1 << 24)) != 0 else 0)
            self.host.emit(4, (rcc_cfgr >> 2) & 0x3)
            self.host.emit(5, rcc_csr)
            return
        raise NativeLoaderError("unknown simulation")

    def _failure(self, reason: str) -> LoadedModule:
        module = self._require_loaded()
        if module.state == LifecycleState.QUARANTINED:
            raise NativeLoaderError("module quarantined")
        failures = module.failures + 1
        if failures >= self.failure_threshold:
            self.loaded = replace(
                module,
                failures=failures,
                state=LifecycleState.QUARANTINED,
                quarantine_reason=reason,
            )
        else:
            self.loaded = replace(module, failures=failures, state=LifecycleState.STOPPED)
        return self.loaded

    def _require_loaded(self) -> LoadedModule:
        if self.loaded is None:
            raise NativeLoaderError("no loaded module")
        return self.loaded


def _u32_arg(text: str) -> int:
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


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate or simulate an EXP069 native module package")
    parser.add_argument("package", type=Path)
    parser.add_argument("--public-key", required=True, type=_load_public_key)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--rcc-cr", type=_u32_arg, default=0)
    parser.add_argument("--rcc-cfgr", type=_u32_arg, default=0)
    parser.add_argument("--rcc-csr", type=_u32_arg, default=0)
    args = parser.parse_args(argv)

    manager = NativeModuleManager(host=NativeHost(rcc=(args.rcc_cr, args.rcc_cfgr, args.rcc_csr)))
    native_package = manager.verify(args.package.read_bytes(), args.public_key)
    print(f"valid native module 0x{native_package.module_id:08X} version {native_package.module_version}")
    print(f"fingerprint {native_package.package_fingerprint.hex()}")
    if args.run:
        manager.load(native_package)
        manager.initialize()
        manager.run()
        for channel, value in manager.host.outputs or []:
            print(f"out {channel} 0x{value:08X}")
        manager.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
