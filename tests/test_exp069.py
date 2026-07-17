from __future__ import annotations

import hashlib
import subprocess
import sys
import unittest
from dataclasses import replace
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

from bytecode_asm import assemble_text
from bytecode_vm import CAP_OUTPUT, CAP_RCC_READ, run_package
from module_format import TYPE_BYTECODE, TYPE_NATIVE, build_package
from nacl.signing import SigningKey
from native_loader import (
    APIEntry,
    API_EMIT,
    API_READ_RCC,
    LifecycleState,
    NativeHeader,
    NativeHost,
    NativeLoaderError,
    NativeModuleManager,
    PlatformAPI,
    build_native_payload,
    build_rcc_analysis_native_payload,
    load_native_package,
    pack_native_header,
    unpack_native_header,
)


ROOT = Path(__file__).parents[1]


def _native_package(
    key: SigningKey | None = None,
    *,
    module_id: int = 0x6901,
    version: int = 3,
    min_platform: int = 1,
    abi_version: int = 1,
    capabilities=(CAP_RCC_READ, CAP_OUTPUT),
    payload: bytes | None = None,
) -> tuple[SigningKey, bytes, bytes]:
    key = key if key is not None else SigningKey.generate()
    payload = build_rcc_analysis_native_payload() if payload is None else payload
    native_header = unpack_native_header(payload)
    package = build_package(
        payload,
        module_id=module_id,
        version=version,
        seed=bytes(key),
        capabilities=capabilities,
        module_type=TYPE_NATIVE,
        min_platform=min_platform,
        abi_version=abi_version,
        code_size=native_header.code_size,
        rodata_size=native_header.rodata_size,
        data_size=native_header.data_size,
        bss_size=native_header.bss_size,
        stack_size=native_header.stack_size,
        entry_offset=native_header.entry_offset,
    )
    return key, package, payload


def _replace_native_header(payload: bytes, **changes: object) -> bytes:
    header = replace(unpack_native_header(payload), **changes)
    return pack_native_header(header) + payload[header.header_size:]


class Exp069NativeLoaderTests(unittest.TestCase):
    def test_valid_native_package_lifecycle_and_simulation(self):
        key, package, _ = _native_package()
        host = NativeHost(rcc=(0x01000002, 0x00000008, 0x08000000))
        manager = NativeModuleManager(host=host)

        native_package = manager.verify(
            package,
            bytes(key.verify_key),
            expected_module_id=0x6901,
            min_module_version=3,
        )
        self.assertEqual(native_package.capabilities, frozenset({CAP_RCC_READ, CAP_OUTPUT}))

        manager.load(native_package)
        self.assertEqual(manager.initialize().state, LifecycleState.INITIALIZED)
        self.assertEqual(manager.run().state, LifecycleState.RUNNING)
        self.assertEqual(
            host.outputs,
            [
                (1, 0x01000002),
                (2, 1),
                (3, 1),
                (4, 2),
                (5, 0x08000000),
            ],
        )
        self.assertEqual(manager.stop().state, LifecycleState.STOPPED)

    def test_invalid_signature_and_wrong_signer_are_rejected(self):
        key, package, _ = _native_package()
        tampered = bytearray(package)
        tampered[-1] ^= 1

        with self.assertRaisesRegex(ValueError, "payload hash|invalid signature"):
            load_native_package(bytes(tampered), bytes(key.verify_key))

        wrong_key = SigningKey.generate()
        with self.assertRaisesRegex(ValueError, "key id mismatch"):
            load_native_package(package, bytes(wrong_key.verify_key))

        wrong_expected = hashlib.sha256(bytes(wrong_key.verify_key)).digest()[:16]
        with self.assertRaisesRegex(NativeLoaderError, "signer identity"):
            load_native_package(package, bytes(key.verify_key), expected_signer_key_id=wrong_expected)

    def test_wrong_abi_platform_and_rollback_are_rejected(self):
        key, wrong_abi, _ = _native_package(abi_version=2)
        with self.assertRaisesRegex(ValueError, "incompatible abi"):
            load_native_package(wrong_abi, bytes(key.verify_key), abi_version=1)

        key, unsupported_platform, _ = _native_package(min_platform=2)
        with self.assertRaisesRegex(ValueError, "incompatible platform"):
            load_native_package(unsupported_platform, bytes(key.verify_key), min_platform=1)

        key, package, _ = _native_package(version=2)
        with self.assertRaisesRegex(ValueError, "rollback version"):
            load_native_package(package, bytes(key.verify_key), min_module_version=3)

    def test_malformed_offsets_integer_overflow_and_entry_validation(self):
        key = SigningKey.generate()
        valid_payload = build_rcc_analysis_native_payload()
        valid_header = unpack_native_header(valid_payload)

        malformed = _replace_native_header(valid_payload, rodata_offset=valid_header.code_offset)
        _, package, _ = _native_package(key, payload=malformed)
        with self.assertRaisesRegex(NativeLoaderError, "overlapping|malformed native offsets"):
            load_native_package(package, bytes(key.verify_key))

        overflow = _replace_native_header(valid_payload, code_offset=0xFFFFFFF0)
        _, package, _ = _native_package(key, payload=overflow)
        with self.assertRaisesRegex(NativeLoaderError, "invalid code range"):
            load_native_package(package, bytes(key.verify_key))

        unaligned_entry = _replace_native_header(valid_payload, entry_offset=1)
        native_header = unpack_native_header(unaligned_entry)
        package = build_package(
            unaligned_entry,
            module_id=0x6901,
            version=1,
            seed=bytes(key),
            capabilities=[CAP_RCC_READ, CAP_OUTPUT],
            module_type=TYPE_NATIVE,
            code_size=native_header.code_size,
            rodata_size=native_header.rodata_size,
            data_size=native_header.data_size,
            bss_size=native_header.bss_size,
            stack_size=native_header.stack_size,
            entry_offset=native_header.entry_offset,
        )
        with self.assertRaisesRegex(NativeLoaderError, "unaligned entry"):
            load_native_package(package, bytes(key.verify_key))

        outside_entry = _replace_native_header(valid_payload, entry_offset=4)
        native_header = unpack_native_header(outside_entry)
        package = build_package(
            outside_entry,
            module_id=0x6901,
            version=1,
            seed=bytes(key),
            capabilities=[CAP_RCC_READ, CAP_OUTPUT],
            module_type=TYPE_NATIVE,
            code_size=native_header.code_size,
            rodata_size=native_header.rodata_size,
            data_size=native_header.data_size,
            bss_size=native_header.bss_size,
            stack_size=native_header.stack_size,
            entry_offset=native_header.entry_offset,
        )
        with self.assertRaisesRegex(NativeLoaderError, "entry outside code"):
            load_native_package(package, bytes(key.verify_key))

    def test_forbidden_capability_and_excessive_resources_are_rejected(self):
        key, package, _ = _native_package(capabilities=[CAP_RCC_READ, CAP_OUTPUT, 0xDEAD])
        with self.assertRaisesRegex(ValueError, "unsupported capability"):
            load_native_package(package, bytes(key.verify_key))

        key = SigningKey.generate()
        valid_payload = build_rcc_analysis_native_payload()
        payload = _replace_native_header(valid_payload, stack_size=2048)
        header = unpack_native_header(payload)
        package = build_package(
            payload,
            module_id=0x6901,
            version=1,
            seed=bytes(key),
            capabilities=[CAP_RCC_READ, CAP_OUTPUT],
            module_type=TYPE_NATIVE,
            code_size=header.code_size,
            rodata_size=header.rodata_size,
            data_size=header.data_size,
            bss_size=header.bss_size,
            stack_size=header.stack_size,
            entry_offset=header.entry_offset,
        )
        with self.assertRaisesRegex(NativeLoaderError, "excessive stack"):
            load_native_package(package, bytes(key.verify_key))

        payload = _replace_native_header(valid_payload, bss_size=800, stack_size=512)
        header = unpack_native_header(payload)
        package = build_package(
            payload,
            module_id=0x6902,
            version=1,
            seed=bytes(key),
            capabilities=[CAP_RCC_READ, CAP_OUTPUT],
            module_type=TYPE_NATIVE,
            code_size=header.code_size,
            rodata_size=header.rodata_size,
            data_size=header.data_size,
            bss_size=header.bss_size,
            stack_size=header.stack_size,
            entry_offset=header.entry_offset,
        )
        with self.assertRaisesRegex(NativeLoaderError, "excessive RAM"):
            load_native_package(package, bytes(key.verify_key))

    def test_malformed_api_table(self):
        key, package, _ = _native_package()
        malformed_api = PlatformAPI(entries=(APIEntry(API_READ_RCC, "read_rcc", CAP_RCC_READ, 1),))
        with self.assertRaisesRegex(NativeLoaderError, "malformed API table"):
            load_native_package(package, bytes(key.verify_key), api=malformed_api)

        duplicate_api = PlatformAPI(
            entries=(
                APIEntry(API_READ_RCC, "read_rcc", CAP_RCC_READ, 1),
                APIEntry(API_READ_RCC, "emit", CAP_OUTPUT, 2),
            )
        )
        with self.assertRaisesRegex(NativeLoaderError, "malformed API table"):
            load_native_package(package, bytes(key.verify_key), api=duplicate_api)

    def test_lifecycle_failure_and_quarantine(self):
        key, package, _ = _native_package()
        manager = NativeModuleManager(host=NativeHost(force_run_failure=True), failure_threshold=2)
        native_package = manager.verify(package, bytes(key.verify_key))
        manager.load(native_package)
        manager.initialize()

        self.assertEqual(manager.run().state, LifecycleState.STOPPED)
        quarantined = manager.run()
        self.assertEqual(quarantined.state, LifecycleState.QUARANTINED)
        self.assertEqual(quarantined.failures, 2)

    def test_exp068_rejects_native_and_native_loader_rejects_bytecode(self):
        key, native_package, _ = _native_package()
        with self.assertRaisesRegex(Exception, "native modules"):
            run_package(native_package, bytes(key.verify_key))

        bytecode = assemble_text("HALT\n").bytecode
        bytecode_package = build_package(
            bytecode,
            module_id=0x68,
            version=1,
            seed=bytes(key),
            capabilities=[],
            module_type=TYPE_BYTECODE,
        )
        with self.assertRaisesRegex(NativeLoaderError, "bytecode package"):
            load_native_package(bytecode_package, bytes(key.verify_key))

    def test_native_loader_cli(self):
        key, package, _ = _native_package()
        with TemporaryDirectory() as tmpdir_name:
            package_path = Path(tmpdir_name) / "native.mod"
            package_path.write_bytes(package)

            run = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "native_loader.py"),
                    str(package_path),
                    "--public-key",
                    bytes(key.verify_key).hex(),
                    "--run",
                    "--rcc-cr",
                    "0x3",
                    "--rcc-cfgr",
                    "0x4",
                    "--rcc-csr",
                    "0x08000000",
                ],
                check=True,
                text=True,
                capture_output=True,
            )
            self.assertIn("valid native module 0x00006901 version 3", run.stdout)
            self.assertIn("out 2 0x00000001", run.stdout)


if __name__ == "__main__":
    unittest.main()
