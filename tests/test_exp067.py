from __future__ import annotations

import subprocess
import sys
import unittest
import zlib
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))

import module_format
from module_catalog import decode, encode, select_newest, select_newest_from_blob
from module_format import build_package, parse_package
from nacl.signing import SigningKey


ROOT = Path(__file__).parents[1]


def _signed_test_package(payload: bytes = b"abc", *, capabilities=(1, 7)):
    key = SigningKey.generate()
    package = build_package(
        payload,
        module_id=0x1234,
        version=2,
        seed=bytes(key),
        capabilities=capabilities,
    )
    return key, package


class Exp067ModuleFormatTests(unittest.TestCase):
    def test_roundtrip(self):
        key, package = _signed_test_package()
        manifest = parse_package(package, bytes(key.verify_key))

        self.assertEqual(manifest.module_id, 0x1234)
        self.assertEqual(manifest.module_version, 2)
        self.assertEqual(manifest.capabilities, (1, 7))

    def test_payload_hash_is_enforced(self):
        key, package = _signed_test_package()
        tampered = bytearray(package)
        tampered[-1] ^= 0x01

        with self.assertRaisesRegex(ValueError, "payload hash"):
            parse_package(bytes(tampered), bytes(key.verify_key))

    def test_signature_covers_crc_included_header(self):
        key, package = _signed_test_package()
        header = module_format._unpack(package)
        capability_end = header.capability_offset + header.capability_size
        payload = package[header.payload_offset:]

        old_buggy_message = (
            module_format._header_for_crc(header)
            + package[header.capability_offset:capability_end]
            + payload
        )
        old_buggy_signature = key.sign(old_buggy_message).signature
        old_buggy_header = replace(header, signature=old_buggy_signature)
        old_buggy_package = (
            module_format._pack_header(
                old_buggy_header,
                signature_zero=False,
                crc_zero=False,
            )
            + package[module_format.HEADER_SIZE:]
        )

        with self.assertRaisesRegex(ValueError, "invalid signature"):
            parse_package(old_buggy_package, bytes(key.verify_key))

    def test_unsigned_gap_between_header_and_capabilities_is_rejected(self):
        key, package = _signed_test_package()
        header = module_format._unpack(package)
        capability_end = header.capability_offset + header.capability_size
        capabilities = package[header.capability_offset:capability_end]
        payload = package[header.payload_offset:]
        gap = b"\x5a" * module_format.CAPABILITY_SIZE

        shifted = replace(
            header,
            total_size=header.total_size + len(gap),
            capability_offset=header.capability_offset + len(gap),
            payload_offset=header.payload_offset + len(gap),
            header_crc32=0,
            signature=b"\0" * 64,
        )
        shifted = replace(
            shifted,
            header_crc32=zlib.crc32(module_format._header_for_crc(shifted)) & module_format.UINT32_MAX,
        )
        signed = module_format._header_for_signature(shifted) + capabilities + payload
        shifted = replace(shifted, signature=key.sign(signed).signature)
        package_with_gap = (
            module_format._pack_header(shifted, signature_zero=False, crc_zero=False)
            + gap
            + capabilities
            + payload
        )

        with self.assertRaisesRegex(ValueError, "noncanonical region layout"):
            parse_package(package_with_gap, bytes(key.verify_key))

    def test_duplicate_and_unsupported_capabilities_are_rejected(self):
        key = SigningKey.generate()

        with self.assertRaisesRegex(ValueError, "duplicate capability"):
            build_package(b"abc", module_id=1, version=1, seed=bytes(key), capabilities=[1, 1])

        package = build_package(b"abc", module_id=1, version=1, seed=bytes(key), capabilities=[7])
        with self.assertRaisesRegex(ValueError, "unsupported capability"):
            parse_package(package, bytes(key.verify_key), supported_capabilities={1})

    def test_malformed_ranges_flags_and_rollback_are_rejected(self):
        key, package = _signed_test_package()
        header = module_format._unpack(package)

        with self.assertRaisesRegex(ValueError, "rollback version"):
            parse_package(package, bytes(key.verify_key), min_module_version=3)

        bad_flags = replace(header, flags=1)
        bad_flags_package = (
            module_format._pack_header(bad_flags, signature_zero=False, crc_zero=False)
            + package[module_format.HEADER_SIZE:]
        )
        with self.assertRaisesRegex(ValueError, "unsupported flags"):
            parse_package(bad_flags_package)

        bad_range = replace(header, payload_offset=module_format.HEADER_SIZE)
        bad_range_package = (
            module_format._pack_header(bad_range, signature_zero=False, crc_zero=False)
            + package[module_format.HEADER_SIZE:]
        )
        with self.assertRaisesRegex(ValueError, "unexpected data between regions|overlapping"):
            parse_package(bad_range_package)

        bad_reserved = replace(header, reserved=b"\x01" + (b"\0" * 15))
        bad_reserved_package = (
            module_format._pack_header(bad_reserved, signature_zero=False, crc_zero=False)
            + package[module_format.HEADER_SIZE:]
        )
        with self.assertRaisesRegex(ValueError, "reserved"):
            parse_package(bad_reserved_package)

    def test_cli_pack_verify_inspect(self):
        key = SigningKey.generate()
        public_key_hex = bytes(key.verify_key).hex()

        with self.subTest("subprocess tools"):
            from tempfile import TemporaryDirectory

            with TemporaryDirectory() as tmpdir_name:
                tmpdir = Path(tmpdir_name)
                seed = tmpdir / "seed.bin"
                payload = tmpdir / "payload.bin"
                package = tmpdir / "package.mod"
                seed.write_bytes(bytes(key))
                payload.write_bytes(b"payload")

                subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "tools" / "module_pack.py"),
                        "--payload",
                        str(payload),
                        "--seed",
                        str(seed),
                        "--id",
                        "0x42",
                        "--version",
                        "3",
                        "--cap",
                        "1",
                        "--output",
                        str(package),
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                )

                verify = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "tools" / "module_verify.py"),
                        str(package),
                        "--public-key",
                        public_key_hex,
                        "--min-module-version",
                        "3",
                        "--supported-cap",
                        "1",
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                )
                self.assertIn("valid module 0x00000042 version 3", verify.stdout)

                inspect = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "tools" / "module_inspect.py"),
                        str(package),
                        "--public-key",
                        public_key_hex,
                    ],
                    check=True,
                    text=True,
                    capture_output=True,
                )
                self.assertIn("module_id=0x00000042", inspect.stdout)
                self.assertIn("signature_verified=yes", inspect.stdout)

    def test_host_tools_with_shebang_are_executable(self):
        for relative in (
            "tools/module_pack.py",
            "tools/module_verify.py",
            "tools/module_inspect.py",
        ):
            with self.subTest(tool=relative):
                self.assertTrue((ROOT / relative).stat().st_mode & 0o111)


class Exp067CatalogTests(unittest.TestCase):
    def test_catalog_roundtrip_and_newest_selection(self):
        first = encode(1, 2, 3, 1, 7, 0, 0, 0, 0, b"a")
        second = encode(2, 2, 4, 1, 7, 1, 0, 0, 0, b"b")
        corrupt = bytearray(encode(3, 2, 5, 1, 7, 1, 0, 0, 0, b"c"))
        corrupt[0] ^= 0xFF

        self.assertEqual(decode(first)[:2], (1, 2))
        self.assertEqual(select_newest([first, bytes(corrupt), second]).sequence, 2)
        self.assertEqual(select_newest_from_blob(first + bytes(corrupt) + second).version, 4)

    def test_catalog_crc_reserved_and_bounds(self):
        record = bytearray(encode(1, 2, 3, 1, 7, 0, 0, 0, 0))
        record[10] ^= 0x01
        with self.assertRaisesRegex(ValueError, "catalog crc"):
            decode(bytes(record))

        record = bytearray(encode(1, 2, 3, 1, 7, 0, 0, 0, 0))
        record[-1] = 1
        with self.assertRaisesRegex(ValueError, "reserved"):
            decode(bytes(record))

        with self.assertRaisesRegex(ValueError, "minimum version"):
            encode(1, 2, 3, 4, 7, 0, 0, 0, 0)

        with self.assertRaisesRegex(ValueError, "catalog blob size"):
            select_newest_from_blob(b"x")


if __name__ == "__main__":
    unittest.main()
