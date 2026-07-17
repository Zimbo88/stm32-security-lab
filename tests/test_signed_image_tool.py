import importlib.util
import os
import re
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SIGNER_PATH = (
    ROOT
    / "firmware"
    / "exp065_signed_app"
    / "tools"
    / "build_signed_image.py"
)

spec = importlib.util.spec_from_file_location("build_signed_image", SIGNER_PATH)
assert spec is not None
signer = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(signer)


SEED = bytes(range(32))


def make_application(
    size: int = 64,
    *,
    msp: int | None = None,
    reset_vector: int | None = None,
) -> bytes:
    if size < 8:
        return b"\x00" * size

    image = bytearray(b"\x00" * size)
    if msp is None:
        msp = signer.APPLICATION_MSP_END
    if reset_vector is None:
        reset_vector = signer.APPLICATION_BASE | 1

    struct.pack_into("<II", image, 0, msp, reset_vector)
    return bytes(image)


class SignedImageToolTests(unittest.TestCase):
    def test_maximum_payload_size_is_accepted(self) -> None:
        image = make_application(signer.MAX_PAYLOAD_SIZE)
        signed, payload_hash, initial_msp, reset_vector = signer.build_signed_image(
            image,
            SEED,
        )

        self.assertEqual(len(signed), signer.APPLICATION_OFFSET + len(image))
        self.assertEqual(payload_hash, signer.hashlib.sha512(image).digest())
        self.assertEqual(initial_msp, signer.APPLICATION_MSP_END)
        self.assertEqual(reset_vector, signer.APPLICATION_BASE | 1)

        manifest = signed[:signer.MANIFEST_SIZE]
        unpacked = signer.MANIFEST_STRUCT.unpack(manifest)
        self.assertEqual(unpacked[0], signer.SIGNED_IMAGE_MAGIC)
        self.assertEqual(unpacked[1], signer.SIGNED_HEADER_VERSION)
        self.assertEqual(unpacked[3], signer.APPLICATION_BASE)
        self.assertEqual(unpacked[4], signer.MAX_PAYLOAD_SIZE)
        self.assertEqual(unpacked[5], 0)
        self.assertEqual(unpacked[6], 0)
        self.assertEqual(unpacked[7], 0)

    def test_payload_one_byte_too_large_is_rejected(self) -> None:
        image = make_application(signer.MAX_PAYLOAD_SIZE + 1)

        with self.assertRaisesRegex(
            signer.SigningError,
            "exceeds bootloader-supported application region",
        ):
            signer.build_signed_image(image, SEED)

    def test_invalid_msp_is_rejected(self) -> None:
        image = make_application(msp=signer.APPLICATION_MSP_BASE)

        with self.assertRaisesRegex(
            signer.SigningError,
            "Initial MSP is outside supported SRAM range",
        ):
            signer.build_signed_image(image, SEED)

    def test_unaligned_msp_is_rejected(self) -> None:
        image = make_application(msp=signer.APPLICATION_MSP_END - 4)

        with self.assertRaisesRegex(
            signer.SigningError,
            "Initial MSP is not aligned",
        ):
            signer.build_signed_image(image, SEED)

    def test_invalid_reset_vector_is_rejected(self) -> None:
        reset = (signer.APPLICATION_BASE + 64) | 1
        image = make_application(size=64, reset_vector=reset)

        with self.assertRaisesRegex(
            signer.SigningError,
            "Reset vector is outside application payload",
        ):
            signer.build_signed_image(image, SEED)

    def test_reset_vector_without_thumb_bit_is_rejected(self) -> None:
        image = make_application(reset_vector=signer.APPLICATION_BASE)

        with self.assertRaisesRegex(
            signer.SigningError,
            "does not have the Thumb bit",
        ):
            signer.build_signed_image(image, SEED)

    def test_overflow_boundary_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            signer.SigningError,
            "overflows the 32-bit address space",
        ):
            signer.checked_u32_add(
                signer.UINT32_MAX - 7,
                8,
                "Application address range",
            )

    def test_unsupported_flags_are_rejected(self) -> None:
        image = make_application()

        with self.assertRaisesRegex(
            signer.SigningError,
            "Unsupported signed-image flags",
        ):
            signer.build_signed_image(image, SEED, flags=1)

    def test_unsupported_header_version_is_rejected(self) -> None:
        image = make_application()

        with self.assertRaisesRegex(
            signer.SigningError,
            "Unsupported signed-image header version",
        ):
            signer.build_signed_image(image, SEED, header_version=2)

    def test_noncanonical_reserved_fields_are_rejected(self) -> None:
        image = make_application()

        with self.assertRaisesRegex(
            signer.SigningError,
            "Reserved manifest fields must be zero",
        ):
            signer.build_signed_image(image, SEED, reserved0=1)

    def test_truncated_application_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            signer.SigningError,
            "too small to contain a vector table",
        ):
            signer.build_signed_image(b"\x00" * 7, SEED)

    def test_atomic_output_failure_preserves_existing_file(self) -> None:
        image = make_application()
        signed, _, _, _ = signer.build_signed_image(image, SEED)

        with tempfile.TemporaryDirectory() as tmpdir_name:
            tmpdir = Path(tmpdir_name)
            output = tmpdir / "signed.bin"
            output.write_bytes(b"existing")

            def fail_replace(src: os.PathLike[str], dst: os.PathLike[str]) -> None:
                raise OSError("simulated replace failure")

            with mock.patch.object(signer.os, "replace", side_effect=fail_replace):
                with self.assertRaisesRegex(
                    signer.SigningError,
                    "Failed to write output atomically",
                ):
                    signer.write_output_atomically(output, signed)

            self.assertEqual(output.read_bytes(), b"existing")
            self.assertEqual(list(tmpdir.glob(".signed.bin.*.tmp")), [])

    def test_make_signing_targets_require_explicit_seed_path(self) -> None:
        for relative in (
            "firmware/exp065_signed_app/Makefile",
            "firmware/exp066_research_platform_core/Makefile",
        ):
            text = (ROOT / relative).read_text(encoding="ascii")
            with self.subTest(makefile=relative):
                self.assertRegex(text, re.compile(r"^SIGNING_SEED\s*\?=\s*$", re.M))
                self.assertIn("require-signing-seed", text)
                self.assertIn("set SIGNING_SEED to an explicit", text)
                self.assertNotIn("SIGNING_SEED ?= ../exp065_signed_app/keys", text)
                self.assertNotIn("SIGNING_SEED    := keys/", text)


if __name__ == "__main__":
    unittest.main()
