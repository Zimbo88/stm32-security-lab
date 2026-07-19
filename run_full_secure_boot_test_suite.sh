#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$HOME/stm32-security-lab"
BOOT_DIR="$ROOT/firmware/exp045_bootloader_v2"
APP_DIR="$ROOT/firmware/exp066_research_platform_core"

BOOT="$BOOT_DIR/build/exp045_bootloader_v2.bin"
REFERENCE="$APP_DIR/build/exp066_research_platform_core_slot_a_update_v2.bin"
SEED="../exp065_signed_app/keys/firmware_signing_seed.bin"

FLASH_BOOT_ADDRESS="0x08000000"
FLASH_SLOT_A_ADDRESS="0x08020000"

TEST_DIR="$APP_DIR/build/full_security_tests"
RESULT_DIR="$ROOT/test-results/secure-boot-$(date +%Y%m%d-%H%M%S)"
LOG="$RESULT_DIR/terminal.log"
SUMMARY="$RESULT_DIR/summary.txt"

WAIT_SECONDS="${WAIT_SECONDS:-10}"
RESET_CYCLES="${RESET_CYCLES:-10}"

mkdir -p "$RESULT_DIR"

exec > >(tee -a "$LOG") 2>&1

PASSED=0
FAILED=0
SKIPPED=0

section() {
    printf '\n'
    printf '%s\n' "================================================================"
    printf '%s\n' "$1"
    printf '%s\n' "================================================================"
}

pass() {
    PASSED=$((PASSED + 1))
    echo "[PASS] $1"
}

fail() {
    FAILED=$((FAILED + 1))
    echo "[FAIL] $1"
}

skip() {
    SKIPPED=$((SKIPPED + 1))
    echo "[SKIP] $1"
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: required command not found: $1"
        exit 1
    }
}

restore_valid_image() {
    set +e

    if [[ -f "$BOOT" && -f "$REFERENCE" ]]; then
        section "RESTORING VALID BOOTLOADER AND REFERENCE IMAGE"

        st-flash --reset write "$BOOT" "$FLASH_BOOT_ADDRESS"
        st-flash --reset write "$REFERENCE" "$FLASH_SLOT_A_ADDRESS"

        echo "Valid reference image restored."
    else
        echo "WARNING: valid binaries unavailable; automatic restore was not possible."
    fi

    set -e
}

trap restore_valid_image EXIT INT TERM

require_command make
require_command python3
require_command sha256sum
require_command cmp
require_command st-flash
require_command arm-none-eabi-gcc

section "ENVIRONMENT"

date --iso-8601=seconds
uname -a
arm-none-eabi-gcc --version | head -n 1
st-flash --version 2>&1 | head -n 1 || true
python3 --version
git -C "$ROOT" status --short || true
git -C "$ROOT" rev-parse HEAD || true

section "HOST TESTS"

if command -v pytest >/dev/null 2>&1; then
    if pytest -q "$ROOT/tests"; then
        pass "Repository pytest suite"
    else
        fail "Repository pytest suite"
    fi
else
    skip "pytest is not installed"
fi

section "BUILD BOOTLOADER"

if make -C "$BOOT_DIR" clean all LAYOUT_PROFILE=stm32f429_1m; then
    pass "Bootloader build"
else
    fail "Bootloader build"
    exit 1
fi

section "BUILD VALID SLOT-A V2 PACKAGE"

if make -C "$APP_DIR" SLOT=a clean all verify-signed \
    LAYOUT_PROFILE=stm32f429_1m \
    SIGNING_SEED="$SEED" \
    PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h
then
    pass "EXP066 signed Slot-A v2 package build"
else
    fail "EXP066 signed Slot-A v2 package build"
    exit 1
fi

[[ -f "$BOOT" ]] || {
    echo "ERROR: missing bootloader binary: $BOOT"
    exit 1
}

[[ -f "$REFERENCE" ]] || {
    echo "ERROR: missing reference package: $REFERENCE"
    exit 1
}

cp "$BOOT" "$RESULT_DIR/bootloader-reference.bin"
cp "$REFERENCE" "$RESULT_DIR/application-reference-build-1.bin"

section "PACKAGE HASHES"

sha256sum "$BOOT" "$REFERENCE" | tee "$RESULT_DIR/reference-sha256.txt"

section "BUILD REPRODUCIBILITY"

FIRST_BOOT_HASH="$(sha256sum "$BOOT" | awk '{print $1}')"
FIRST_APP_HASH="$(sha256sum "$REFERENCE" | awk '{print $1}')"

make -C "$BOOT_DIR" clean all LAYOUT_PROFILE=stm32f429_1m

make -C "$APP_DIR" SLOT=a clean all verify-signed \
    LAYOUT_PROFILE=stm32f429_1m \
    SIGNING_SEED="$SEED" \
    PUBLIC_KEY_HEADER=../exp045_bootloader_v2/src/firmware_public_key.h

SECOND_BOOT_HASH="$(sha256sum "$BOOT" | awk '{print $1}')"
SECOND_APP_HASH="$(sha256sum "$REFERENCE" | awk '{print $1}')"

cp "$BOOT" "$RESULT_DIR/bootloader-reference-build-2.bin"
cp "$REFERENCE" "$RESULT_DIR/application-reference-build-2.bin"

{
    echo "Bootloader build 1: $FIRST_BOOT_HASH"
    echo "Bootloader build 2: $SECOND_BOOT_HASH"
    echo "Application build 1: $FIRST_APP_HASH"
    echo "Application build 2: $SECOND_APP_HASH"
} | tee "$RESULT_DIR/reproducibility.txt"

if [[ "$FIRST_BOOT_HASH" == "$SECOND_BOOT_HASH" ]]; then
    pass "Bootloader reproducible build"
else
    fail "Bootloader binaries differ between clean builds"
fi

if [[ "$FIRST_APP_HASH" == "$SECOND_APP_HASH" ]]; then
    pass "Signed update package reproducible build"
else
    fail "Signed update packages differ between clean builds"
fi

section "CREATE HARDWARE TEST IMAGES"

rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

REFERENCE="$REFERENCE" TEST_DIR="$TEST_DIR" python3 - <<'PY'
from pathlib import Path
import os
import struct
import hashlib
import json

source = Path(os.environ["REFERENCE"])
output = Path(os.environ["TEST_DIR"])
reference = source.read_bytes()

SIGNATURE_OFFSET = 0x60
SIGNATURE_SIZE = 0x40
PAYLOAD_OFFSET = 0x200

if len(reference) < PAYLOAD_OFFSET + 8:
    raise SystemExit("Reference update package is too small")

fields = struct.unpack_from("<8I", reference, 0)
(
    magic,
    header_version,
    image_version,
    vector_address,
    image_size,
    flags,
    reserved0,
    reserved1,
) = fields

manifest = {
    "magic": f"0x{magic:08X}",
    "header_version": header_version,
    "image_version": image_version,
    "vector_address": f"0x{vector_address:08X}",
    "image_size": image_size,
    "flags": f"0x{flags:08X}",
    "reserved0": f"0x{reserved0:08X}",
    "reserved1": f"0x{reserved1:08X}",
    "package_size": len(reference),
    "sha256": hashlib.sha256(reference).hexdigest(),
}

(output / "manifest.json").write_text(
    json.dumps(manifest, indent=2) + "\n",
    encoding="utf-8",
)

print(json.dumps(manifest, indent=2))

tests = []

def add(name, expected, mutation):
    data = bytearray(reference)
    mutation(data)
    path = output / name
    path.write_bytes(data)

    tests.append({
        "file": name,
        "expected": expected,
        "sha256": hashlib.sha256(data).hexdigest(),
    })

def write_u32(data, offset, value):
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)

add(
    "01_valid_reference.bin",
    "Verification OK; EXP066 starts",
    lambda data: None,
)

add(
    "02_payload_first_data_byte_corrupt.bin",
    "PAYLOAD SHA512 MISMATCH",
    lambda data: data.__setitem__(
        PAYLOAD_OFFSET + 0x20,
        data[PAYLOAD_OFFSET + 0x20] ^ 0x01,
    ),
)

add(
    "03_payload_last_byte_corrupt.bin",
    "PAYLOAD SHA512 MISMATCH",
    lambda data: data.__setitem__(
        len(data) - 1,
        data[-1] ^ 0x80,
    ),
)

add(
    "04_signature_first_byte_corrupt.bin",
    "ED25519 SIGNATURE INVALID",
    lambda data: data.__setitem__(
        SIGNATURE_OFFSET,
        data[SIGNATURE_OFFSET] ^ 0x01,
    ),
)

add(
    "05_signature_last_byte_corrupt.bin",
    "ED25519 SIGNATURE INVALID",
    lambda data: data.__setitem__(
        SIGNATURE_OFFSET + SIGNATURE_SIZE - 1,
        data[SIGNATURE_OFFSET + SIGNATURE_SIZE - 1] ^ 0x80,
    ),
)

add(
    "06_bad_magic_zero.bin",
    "BAD MAGIC",
    lambda data: write_u32(data, 0x00, 0),
)

add(
    "07_bad_magic_single_bit.bin",
    "BAD MAGIC",
    lambda data: write_u32(data, 0x00, magic ^ 1),
)

add(
    "08_bad_header_version_zero.bin",
    "BAD HEADER VERSION",
    lambda data: write_u32(data, 0x04, 0),
)

add(
    "09_bad_header_version_max.bin",
    "BAD HEADER VERSION",
    lambda data: write_u32(data, 0x04, 0xFFFFFFFF),
)

add(
    "10_image_size_zero.bin",
    "BAD PAYLOAD RANGE",
    lambda data: write_u32(data, 0x10, 0),
)

add(
    "11_image_size_one.bin",
    "BAD PAYLOAD RANGE",
    lambda data: write_u32(data, 0x10, 1),
)

add(
    "12_image_size_max.bin",
    "BAD PAYLOAD RANGE",
    lambda data: write_u32(data, 0x10, 0xFFFFFFFF),
)

add(
    "13_vector_address_zero.bin",
    "BAD VECTOR ADDRESS",
    lambda data: write_u32(data, 0x0C, 0),
)

add(
    "14_vector_address_bootloader.bin",
    "BAD VECTOR ADDRESS",
    lambda data: write_u32(data, 0x0C, 0x08000000),
)

add(
    "15_vector_address_unaligned.bin",
    "BAD VECTOR ADDRESS",
    lambda data: write_u32(data, 0x0C, vector_address + 1),
)

add(
    "16_initial_msp_zero.bin",
    "BAD INITIAL MSP",
    lambda data: write_u32(data, PAYLOAD_OFFSET, 0),
)

add(
    "17_initial_msp_erased.bin",
    "BAD INITIAL MSP",
    lambda data: write_u32(data, PAYLOAD_OFFSET, 0xFFFFFFFF),
)

add(
    "18_initial_msp_flash_address.bin",
    "BAD INITIAL MSP",
    lambda data: write_u32(data, PAYLOAD_OFFSET, 0x08020200),
)

add(
    "19_initial_msp_unaligned.bin",
    "BAD INITIAL MSP",
    lambda data: write_u32(data, PAYLOAD_OFFSET, 0x20000001),
)

add(
    "20_reset_vector_zero.bin",
    "BAD RESET VECTOR",
    lambda data: write_u32(data, PAYLOAD_OFFSET + 4, 0),
)

add(
    "21_reset_vector_erased.bin",
    "BAD RESET VECTOR",
    lambda data: write_u32(data, PAYLOAD_OFFSET + 4, 0xFFFFFFFF),
)

add(
    "22_reset_vector_bootloader.bin",
    "BAD RESET VECTOR",
    lambda data: write_u32(data, PAYLOAD_OFFSET + 4, 0x08000001),
)

add(
    "23_reset_vector_thumb_bit_clear.bin",
    "BAD RESET VECTOR",
    lambda data: write_u32(
        data,
        PAYLOAD_OFFSET + 4,
        struct.unpack_from("<I", data, PAYLOAD_OFFSET + 4)[0] & ~1,
    ),
)

add(
    "24_flags_unsupported.bin",
    "Rejected flags or invalid signature",
    lambda data: write_u32(data, 0x14, 0xFFFFFFFF),
)

add(
    "25_reserved0_corrupt.bin",
    "Rejected target/layout or invalid signature",
    lambda data: write_u32(data, 0x18, reserved0 ^ 1),
)

add(
    "26_reserved1_corrupt.bin",
    "Rejected slot/layout or invalid signature",
    lambda data: write_u32(data, 0x1C, reserved1 ^ 1),
)

(output / "tests.json").write_text(
    json.dumps(tests, indent=2) + "\n",
    encoding="utf-8",
)

for index, test in enumerate(tests, start=1):
    print(
        f"{index:02d}. {test['file']}: "
        f"{test['expected']} [{test['sha256']}]"
    )
PY

cp "$TEST_DIR/manifest.json" "$RESULT_DIR/"
cp "$TEST_DIR/tests.json" "$RESULT_DIR/"

section "FLASH BOOTLOADER"

st-flash --reset write "$BOOT" "$FLASH_BOOT_ADDRESS"
pass "Bootloader flashed and verified"

flash_test() {
    local file="$1"
    local expected="$2"
    local full_path="$TEST_DIR/$file"

    section "HARDWARE TEST: $file"

    echo "Expected UART result:"
    echo "  $expected"
    echo
    echo "Image SHA-256:"
    sha256sum "$full_path"

    st-flash --reset write "$full_path" "$FLASH_SLOT_A_ADDRESS"

    echo
    echo "UART capture window: ${WAIT_SECONDS} seconds"
    echo "Do not enter commands in the application during this interval."
    sleep "$WAIT_SECONDS"
}

while IFS=$'\t' read -r file expected; do
    flash_test "$file" "$expected"
done < <(
    TEST_DIR="$TEST_DIR" python3 - <<'PY'
from pathlib import Path
import json
import os

tests = json.loads(
    (Path(os.environ["TEST_DIR"]) / "tests.json").read_text(encoding="utf-8")
)

for test in tests:
    print(f"{test['file']}\t{test['expected']}")
PY
)

section "RESTORE VALID IMAGE BEFORE RESET STABILITY TEST"

st-flash --reset write "$REFERENCE" "$FLASH_SLOT_A_ADDRESS"
sleep "$WAIT_SECONDS"

section "RESET STABILITY TEST: $RESET_CYCLES CYCLES"

for cycle in $(seq 1 "$RESET_CYCLES"); do
    echo
    echo "------------------------------------------------------------"
    printf 'RESET CYCLE %02d/%02d\n' "$cycle" "$RESET_CYCLES"
    echo "Expected: Verification OK and EXP066 starts"
    echo "------------------------------------------------------------"

    if st-flash reset; then
        echo "Reset command accepted."
    else
        echo "st-flash reset unavailable or failed; using valid image rewrite."
        st-flash --reset write "$REFERENCE" "$FLASH_SLOT_A_ADDRESS"
    fi

    sleep "$WAIT_SECONDS"
done

pass "$RESET_CYCLES reset stability cycles executed"

section "FINAL VALID IMAGE RESTORE"

st-flash --reset write "$BOOT" "$FLASH_BOOT_ADDRESS"
st-flash --reset write "$REFERENCE" "$FLASH_SLOT_A_ADDRESS"
sleep "$WAIT_SECONDS"

pass "Valid bootloader and EXP066 package restored"

trap - EXIT INT TERM

section "TEST-SUITE SUMMARY"

{
    echo "Secure Boot Test Suite"
    echo "Date: $(date --iso-8601=seconds)"
    echo
    echo "Automated host/build checks passed: $PASSED"
    echo "Automated host/build checks failed: $FAILED"
    echo "Automated checks skipped: $SKIPPED"
    echo
    echo "Hardware image tests generated: 26"
    echo "Reset stability cycles: $RESET_CYCLES"
    echo
    echo "UART results must be evaluated from the separate UART capture."
    echo
    echo "Not modified automatically:"
    echo "- Persistent boot metadata copies"
    echo "- Candidate/confirmed state transitions"
    echo "- Rollback counters"
    echo "- Slot-B installation"
    echo "- Recovery-mode inputs"
    echo
    echo "Those require repository-specific provisioning logic and are kept"
    echo "out of this generic test to avoid damaging valid boot state."
} | tee "$SUMMARY"

echo
echo "============================================================"
echo "TEST RUN FINISHED"
echo "============================================================"
echo "Terminal log:"
echo "  $LOG"
echo
echo "Summary:"
echo "  $SUMMARY"
echo
echo "Generated test images:"
echo "  $TEST_DIR"
echo
echo "The valid reference image is currently flashed."
echo "Send the complete UART log and terminal log for evaluation."

if (( FAILED > 0 )); then
    exit 1
fi
