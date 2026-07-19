#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/stm32-security-lab"
BOOT_DIR="${ROOT}/firmware/exp045_bootloader_v2"
APP_DIR="${ROOT}/firmware/exp066_research_platform_core"

BOOT_BIN="${BOOT_DIR}/build/exp045_bootloader_v2.bin"

SLOT_A_PACKAGE="${APP_DIR}/build/exp066_research_platform_core_slot_a_update_v2.bin"
SLOT_B_PACKAGE="${APP_DIR}/build/exp066_research_platform_core_slot_b_update_v2.bin"

SIGNING_SEED="../exp065_signed_app/keys/firmware_signing_seed.bin"
PUBLIC_KEY_HEADER="../exp045_bootloader_v2/src/firmware_public_key.h"
LAYOUT_PROFILE="stm32f429_1m"

BOOT_ADDRESS="0x08000000"
METADATA_A_ADDRESS="0x08008000"
METADATA_B_ADDRESS="0x0800C000"
SLOT_A_ADDRESS="0x08020000"
SLOT_B_ADDRESS="0x08080000"

BOOT_BACKUP_SIZE="0x8000"
METADATA_COPY_SIZE="0x4000"
SLOT_SIZE="0x60000"

WAIT_SECONDS="${WAIT_SECONDS:-10}"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${ROOT}/test-results/slot-policy-${RUN_ID}"
BACKUP_DIR="${RESULT_DIR}/flash-backup"
LOG="${RESULT_DIR}/terminal.log"
SUMMARY="${RESULT_DIR}/summary.txt"

mkdir -p "${BACKUP_DIR}"

exec > >(tee -a "${LOG}") 2>&1

RESTORE_REQUIRED=0
TESTS_RUN=0
FLASH_ERRORS=0

section() {
    printf '\n'
    printf '%s\n' "================================================================"
    printf '%s\n' "$1"
    printf '%s\n' "================================================================"
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: required command not found: $1"
        exit 1
    }
}

flash_write() {
    local file="$1"
    local address="$2"

    echo "Writing:"
    echo "  file    = ${file}"
    echo "  address = ${address}"

    if ! st-flash --reset write "${file}" "${address}"; then
        FLASH_ERRORS=$((FLASH_ERRORS + 1))
        return 1
    fi
}

flash_read() {
    local file="$1"
    local address="$2"
    local size="$3"

    echo "Reading:"
    echo "  file    = ${file}"
    echo "  address = ${address}"
    echo "  size    = ${size}"

    st-flash read "${file}" "${address}" "${size}"
}

capture_window() {
    local expected="$1"

    echo
    echo "Expected or relevant UART behaviour:"
    echo "  ${expected}"
    echo
    echo "UART capture window: ${WAIT_SECONDS} seconds"
    sleep "${WAIT_SECONDS}"
}

restore_original_flash() {
    local previous_status=$?

    set +e

    if (( RESTORE_REQUIRED == 1 )); then
        section "RESTORING ORIGINAL FLASH CONTENT"

        st-flash --reset write \
            "${BACKUP_DIR}/bootloader.bin" \
            "${BOOT_ADDRESS}"

        st-flash --reset write \
            "${BACKUP_DIR}/metadata-a.bin" \
            "${METADATA_A_ADDRESS}"

        st-flash --reset write \
            "${BACKUP_DIR}/metadata-b.bin" \
            "${METADATA_B_ADDRESS}"

        st-flash --reset write \
            "${BACKUP_DIR}/slot-a.bin" \
            "${SLOT_A_ADDRESS}"

        st-flash --reset write \
            "${BACKUP_DIR}/slot-b.bin" \
            "${SLOT_B_ADDRESS}"

        echo
        echo "Original bootloader, metadata and both slots restored."
        echo "Final UART capture window: ${WAIT_SECONDS} seconds"
        sleep "${WAIT_SECONDS}"
    fi

    set -e
    exit "${previous_status}"
}

trap restore_original_flash EXIT INT TERM

for command in \
    make \
    python3 \
    sha256sum \
    cmp \
    st-flash \
    arm-none-eabi-gcc \
    arm-none-eabi-objcopy
do
    require_command "${command}"
done

section "ENVIRONMENT"

date --iso-8601=seconds
uname -a
git -C "${ROOT}" rev-parse HEAD || true
git -C "${ROOT}" status --short || true
st-flash --version || true
arm-none-eabi-gcc --version | head -n 1

section "BACK UP COMPLETE TEST-RELEVANT FLASH"

flash_read \
    "${BACKUP_DIR}/bootloader.bin" \
    "${BOOT_ADDRESS}" \
    "${BOOT_BACKUP_SIZE}"

flash_read \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${METADATA_A_ADDRESS}" \
    "${METADATA_COPY_SIZE}"

flash_read \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${METADATA_B_ADDRESS}" \
    "${METADATA_COPY_SIZE}"

flash_read \
    "${BACKUP_DIR}/slot-a.bin" \
    "${SLOT_A_ADDRESS}" \
    "${SLOT_SIZE}"

flash_read \
    "${BACKUP_DIR}/slot-b.bin" \
    "${SLOT_B_ADDRESS}" \
    "${SLOT_SIZE}"

RESTORE_REQUIRED=1

sha256sum "${BACKUP_DIR}"/*.bin |
    tee "${RESULT_DIR}/flash-backup-sha256.txt"

section "BUILD BOOTLOADER"

make -C "${BOOT_DIR}" clean all \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}"

test -f "${BOOT_BIN}"

cp "${BOOT_BIN}" "${RESULT_DIR}/bootloader-under-test.bin"

section "BUILD SLOT-A PACKAGE"

make -C "${APP_DIR}" SLOT=a clean all verify-signed \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}" \
    SIGNING_SEED="${SIGNING_SEED}" \
    PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER}"

test -f "${SLOT_A_PACKAGE}"

cp "${SLOT_A_PACKAGE}" "${RESULT_DIR}/slot-a-reference.bin"

section "BUILD SLOT-B PACKAGE"

make -C "${APP_DIR}" SLOT=b clean all verify-signed \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}" \
    SIGNING_SEED="${SIGNING_SEED}" \
    PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER}"

if [[ ! -f "${SLOT_B_PACKAGE}" ]]; then
    echo
    echo "ERROR: Slot-B package was not generated:"
    echo "  ${SLOT_B_PACKAGE}"
    echo
    echo "Available update packages:"
    find "${APP_DIR}/build" -maxdepth 1 -type f \
        -name '*update*.bin' -print || true
    exit 1
fi

cp "${SLOT_B_PACKAGE}" "${RESULT_DIR}/slot-b-reference.bin"

section "PACKAGE INFORMATION"

sha256sum \
    "${BOOT_BIN}" \
    "${SLOT_A_PACKAGE}" \
    "${SLOT_B_PACKAGE}" |
    tee "${RESULT_DIR}/packages-sha256.txt"

SLOT_A_PACKAGE="${SLOT_A_PACKAGE}" \
SLOT_B_PACKAGE="${SLOT_B_PACKAGE}" \
python3 - <<'PY' | tee "${RESULT_DIR}/package-manifests.txt"
from pathlib import Path
import os
import struct

for label, variable in (
    ("SLOT A", "SLOT_A_PACKAGE"),
    ("SLOT B", "SLOT_B_PACKAGE"),
):
    path = Path(os.environ[variable])
    data = path.read_bytes()

    if len(data) < 0x200:
        raise SystemExit(f"{label}: package is too small")

    fields = struct.unpack_from("<8I", data, 0)
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

    initial_msp, reset_vector = struct.unpack_from("<2I", data, 0x200)

    print(label)
    print(f"  path            = {path}")
    print(f"  package_size    = {len(data)}")
    print(f"  magic           = 0x{magic:08X}")
    print(f"  header_version  = {header_version}")
    print(f"  image_version   = {image_version}")
    print(f"  vector_address  = 0x{vector_address:08X}")
    print(f"  image_size      = {image_size}")
    print(f"  flags           = 0x{flags:08X}")
    print(f"  reserved0       = 0x{reserved0:08X}")
    print(f"  reserved1       = 0x{reserved1:08X}")
    print(f"  initial_msp     = 0x{initial_msp:08X}")
    print(f"  reset_vector    = 0x{reset_vector:08X}")
    print()
PY

section "CREATE ERASED AND CORRUPTED SLOT IMAGES"

python3 - "${RESULT_DIR}" "${SLOT_SIZE}" <<'PY'
from pathlib import Path
import sys

result_dir = Path(sys.argv[1])
slot_size = int(sys.argv[2], 0)

(result_dir / "erased-slot.bin").write_bytes(b"\xFF" * slot_size)
(result_dir / "zero-slot-header.bin").write_bytes(b"\x00" * 0x200)
PY

ERASED_SLOT="${RESULT_DIR}/erased-slot.bin"
ZERO_SLOT_HEADER="${RESULT_DIR}/zero-slot-header.bin"

run_test() {
    local title="$1"
    local expected="$2"

    TESTS_RUN=$((TESTS_RUN + 1))

    section "TEST ${TESTS_RUN}: ${title}"
    echo "Expected:"
    echo "  ${expected}"
}

section "INSTALL BOOTLOADER UNDER TEST"

flash_write "${BOOT_BIN}" "${BOOT_ADDRESS}"

run_test \
    "BASELINE: ORIGINAL METADATA + VALID SLOT A" \
    "Slot A should verify successfully and EXP066 should start."

flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
capture_window \
    "Verification = OK; Slot decision likely CONFIRMED; EXP066 starts."

run_test \
    "ONLY SLOT A VALID, SLOT B ERASED" \
    "Slot A must boot. Slot B must not influence the decision."

flash_write "${ERASED_SLOT}" "${SLOT_B_ADDRESS}"
flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
capture_window \
    "Slot A boots successfully."

run_test \
    "ONLY SLOT B VALID, SLOT A ERASED" \
    "This reveals whether current metadata or fallback policy permits Slot B."

flash_write "${ERASED_SLOT}" "${SLOT_A_ADDRESS}"
flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"
capture_window \
    "Either Slot B boots, or bootloader reports no bootable selected/confirmed slot. Both outcomes are informative."

run_test \
    "BOTH SLOTS VALID" \
    "Bootloader should select the slot dictated by metadata and policy."

flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"
capture_window \
    "A deterministic slot decision must be reported and the selected image must verify."

run_test \
    "SLOT A HEADER DESTROYED, SLOT B VALID" \
    "Tests automatic fallback from invalid Slot A to valid Slot B."

flash_write "${ZERO_SLOT_HEADER}" "${SLOT_A_ADDRESS}"
flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"
capture_window \
    "Expected policy result: Slot B boots if fallback is implemented; otherwise safe halt."

run_test \
    "SLOT A SIGNATURE CORRUPTED, SLOT B VALID" \
    "Tests fallback after cryptographic rejection of Slot A."

cp "${SLOT_A_PACKAGE}" "${RESULT_DIR}/slot-a-bad-signature.bin"

python3 - "${RESULT_DIR}/slot-a-bad-signature.bin" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
data[0x60] ^= 0x01
path.write_bytes(data)
PY

flash_write \
    "${RESULT_DIR}/slot-a-bad-signature.bin" \
    "${SLOT_A_ADDRESS}"

flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"

capture_window \
    "Slot A must be rejected. Slot B should boot only if verified fallback is implemented."

run_test \
    "SLOT B HEADER DESTROYED, SLOT A VALID" \
    "Slot A must continue to boot despite invalid Slot B."

flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
flash_write "${ZERO_SLOT_HEADER}" "${SLOT_B_ADDRESS}"
capture_window \
    "Slot A verifies and starts."

run_test \
    "BOTH SLOT HEADERS DESTROYED" \
    "No application may start; bootloader must halt safely or enter recovery."

flash_write "${ZERO_SLOT_HEADER}" "${SLOT_A_ADDRESS}"
flash_write "${ZERO_SLOT_HEADER}" "${SLOT_B_ADDRESS}"
capture_window \
    "NO BOOTABLE SLOT; application will not be started; safe halt or recovery."

run_test \
    "BOTH SLOTS ERASED" \
    "No application may start."

flash_write "${ERASED_SLOT}" "${SLOT_A_ADDRESS}"
flash_write "${ERASED_SLOT}" "${SLOT_B_ADDRESS}"
capture_window \
    "NO BOOTABLE SLOT; safe halt or recovery."

section "METADATA COPY A CORRUPTION TEST"

echo "Copy B remains exactly as backed up."
echo "Copy A will be erased for this test and then immediately restored."

flash_write "${ERASED_SLOT}" "${METADATA_A_ADDRESS}"
flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"

capture_window \
    "Bootloader should use Metadata Copy B if redundant-copy recovery is implemented."

flash_write \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${METADATA_A_ADDRESS}"

section "METADATA COPY B CORRUPTION TEST"

echo "Copy A remains exactly as backed up."
echo "Copy B will be erased for this test and then immediately restored."

flash_write "${ERASED_SLOT}" "${METADATA_B_ADDRESS}"
flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"

capture_window \
    "Bootloader should use Metadata Copy A if redundant-copy recovery is implemented."

flash_write \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${METADATA_B_ADDRESS}"

section "BOTH METADATA COPIES INVALID"

echo "Both metadata copies are temporarily erased."
echo "The exact backed-up copies will be restored immediately afterwards."

flash_write "${ERASED_SLOT}" "${METADATA_A_ADDRESS}"
flash_write "${ERASED_SLOT}" "${METADATA_B_ADDRESS}"
flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"

capture_window \
    "Bootloader must use a defined factory/default policy, recovery, or safe halt. It must never jump without successful verification."

flash_write \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${METADATA_A_ADDRESS}"

flash_write \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${METADATA_B_ADDRESS}"

section "REPEATED VALID BOOT TEST"

flash_write "${SLOT_A_PACKAGE}" "${SLOT_A_ADDRESS}"
flash_write "${SLOT_B_PACKAGE}" "${SLOT_B_ADDRESS}"

for cycle in $(seq 1 10); do
    echo
    echo "------------------------------------------------------------"
    printf 'VALID RESET CYCLE %02d/10\n' "${cycle}"
    echo "------------------------------------------------------------"

    if ! st-flash reset; then
        echo "Direct reset failed; rewriting bootloader to trigger reset."
        flash_write "${BOOT_BIN}" "${BOOT_ADDRESS}"
    fi

    sleep "${WAIT_SECONDS}"
done

section "RESTORE ORIGINAL STATE"

st-flash --reset write \
    "${BACKUP_DIR}/bootloader.bin" \
    "${BOOT_ADDRESS}"

st-flash --reset write \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${METADATA_A_ADDRESS}"

st-flash --reset write \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${METADATA_B_ADDRESS}"

st-flash --reset write \
    "${BACKUP_DIR}/slot-a.bin" \
    "${SLOT_A_ADDRESS}"

st-flash --reset write \
    "${BACKUP_DIR}/slot-b.bin" \
    "${SLOT_B_ADDRESS}"

RESTORE_REQUIRED=0
trap - EXIT INT TERM

echo
echo "Original flash state restored."
echo "Final UART capture window: ${WAIT_SECONDS} seconds"
sleep "${WAIT_SECONDS}"

section "SUMMARY"

{
    echo "Slot Policy Test Suite"
    echo "Date: $(date --iso-8601=seconds)"
    echo "Git commit: $(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo
    echo "Tests executed: ${TESTS_RUN}"
    echo "Flash errors: ${FLASH_ERRORS}"
    echo
    echo "Covered:"
    echo "- Valid Slot A baseline"
    echo "- Slot A only"
    echo "- Slot B only"
    echo "- Both slots valid"
    echo "- Invalid Slot A with valid Slot B"
    echo "- Invalid Slot B with valid Slot A"
    echo "- Both slots invalid"
    echo "- Both slots erased"
    echo "- Metadata Copy A invalid"
    echo "- Metadata Copy B invalid"
    echo "- Both metadata copies invalid"
    echo "- Ten repeated valid resets"
    echo
    echo "Original bootloader, metadata and slots were restored."
    echo
    echo "UART output still has to be correlated with the numbered tests."
} | tee "${SUMMARY}"

echo
echo "================================================================"
echo "TEST SUITE FINISHED"
echo "================================================================"
echo "Terminal log:"
echo "  ${LOG}"
echo
echo "Summary:"
echo "  ${SUMMARY}"
echo
echo "Flash backup:"
echo "  ${BACKUP_DIR}"
echo
echo "Send the complete terminal and UART logs for evaluation."

if (( FLASH_ERRORS > 0 )); then
    exit 1
fi
