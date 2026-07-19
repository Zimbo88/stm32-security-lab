#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# STM32F429 Secure Boot Slot/Metadata/Recovery Test Monster v2
#
# Optional:
#   UART_DEVICE=/dev/ttyACM0 WAIT_SECONDS=8 RESET_CYCLES=20 \
#       ./run_slot_policy_monster_v2.sh
#
# Without UART_DEVICE, keep an external UART terminal/logger open.
###############################################################################

ROOT="${HOME}/stm32-security-lab"
BOOT_DIR="${ROOT}/firmware/exp045_bootloader_v2"
APP_DIR="${ROOT}/firmware/exp066_research_platform_core"

LAYOUT_PROFILE="stm32f429_1m"
SIGNING_SEED="../exp065_signed_app/keys/firmware_signing_seed.bin"
PUBLIC_KEY_HEADER="../exp045_bootloader_v2/src/firmware_public_key.h"

BOOT_BUILD_BIN="${BOOT_DIR}/build/exp045_bootloader_v2.bin"
APP_BUILD_DIR="${APP_DIR}/build"

BOOT_ADDRESS="0x08000000"
METADATA_A_ADDRESS="0x08008000"
METADATA_B_ADDRESS="0x0800C000"
SLOT_A_ADDRESS="0x08020000"
SLOT_B_ADDRESS="0x08080000"

BOOT_BACKUP_SIZE="0x8000"
METADATA_COPY_SIZE="0x4000"
SLOT_SIZE="0x60000"

WAIT_SECONDS="${WAIT_SECONDS:-8}"
RESET_CYCLES="${RESET_CYCLES:-10}"
UART_BAUD="${UART_BAUD:-115200}"
UART_DEVICE="${UART_DEVICE:-}"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${ROOT}/test-results/slot-policy-monster-v2-${RUN_ID}"
BACKUP_DIR="${RESULT_DIR}/flash-backup"
CACHE_DIR="${RESULT_DIR}/image-cache"
UART_DIR="${RESULT_DIR}/uart"
REPORT_DIR="${RESULT_DIR}/reports"

TERMINAL_LOG="${RESULT_DIR}/terminal.log"
CSV_REPORT="${REPORT_DIR}/results.csv"
MD_REPORT="${REPORT_DIR}/results.md"
SUMMARY="${REPORT_DIR}/summary.txt"

mkdir -p \
    "${BACKUP_DIR}" \
    "${CACHE_DIR}" \
    "${UART_DIR}" \
    "${REPORT_DIR}"

exec > >(tee -a "${TERMINAL_LOG}") 2>&1

ORIGINAL_STATE_CAPTURED=0
RESTORE_COMPLETED=0
CURRENT_TEST=0

PASS_COUNT=0
FAIL_COUNT=0
OBSERVE_COUNT=0
MANUAL_COUNT=0
ERROR_COUNT=0

START_EPOCH="$(date +%s)"

###############################################################################
# Formatting
###############################################################################

section() {
    printf '\n'
    printf '%s\n' \
        "================================================================"
    printf '%s\n' "$1"
    printf '%s\n' \
        "================================================================"
}

subsection() {
    printf '\n'
    printf '%s\n' \
        "----------------------------------------------------------------"
    printf '%s\n' "$1"
    printf '%s\n' \
        "----------------------------------------------------------------"
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 ||
        die "required command not found: $1"
}

###############################################################################
# Flash helpers
###############################################################################

flash_read() {
    local output="$1"
    local address="$2"
    local size="$3"

    echo "READ  ${address} + ${size}"
    echo "  -> ${output}"

    st-flash read "${output}" "${address}" "${size}"
}

flash_write_no_reset() {
    local input="$1"
    local address="$2"

    [[ -f "${input}" ]] ||
        die "flash input does not exist: ${input}"

    echo "WRITE ${input}"
    echo "  -> ${address}"

    st-flash write "${input}" "${address}"
}

board_reset() {
    echo "RESET board"

    if ! st-flash reset; then
        echo "WARNING: st-flash reset failed."
        return 1
    fi
}

###############################################################################
# Restore logic
###############################################################################

restore_original_flash() {
    local trigger="${1:-normal}"

    if (( ORIGINAL_STATE_CAPTURED == 0 )); then
        echo "No complete backup exists; restore skipped."
        return 0
    fi

    if (( RESTORE_COMPLETED == 1 )); then
        return 0
    fi

    section "RESTORING ORIGINAL FLASH STATE (${trigger})"

    set +e

    flash_write_no_reset \
        "${BACKUP_DIR}/bootloader.bin" \
        "${BOOT_ADDRESS}"

    flash_write_no_reset \
        "${BACKUP_DIR}/metadata-a.bin" \
        "${METADATA_A_ADDRESS}"

    flash_write_no_reset \
        "${BACKUP_DIR}/metadata-b.bin" \
        "${METADATA_B_ADDRESS}"

    flash_write_no_reset \
        "${BACKUP_DIR}/slot-a.bin" \
        "${SLOT_A_ADDRESS}"

    flash_write_no_reset \
        "${BACKUP_DIR}/slot-b.bin" \
        "${SLOT_B_ADDRESS}"

    board_reset

    local restore_status=$?

    if (( restore_status == 0 )); then
        RESTORE_COMPLETED=1
        echo "Original flash state restored successfully."
    else
        echo "WARNING: one or more restore operations failed."
    fi

    set -e
    return "${restore_status}"
}

on_exit() {
    local status=$?

    trap - EXIT INT TERM

    if (( ORIGINAL_STATE_CAPTURED == 1 && RESTORE_COMPLETED == 0 )); then
        restore_original_flash "automatic exit trap" || true
    fi

    exit "${status}"
}

on_interrupt() {
    echo
    echo "Interrupt received. Restoring original state..."
    exit 130
}

trap on_exit EXIT
trap on_interrupt INT TERM

###############################################################################
# UART support
###############################################################################

detect_uart_device() {
    if [[ -n "${UART_DEVICE}" ]]; then
        return
    fi

    local candidate

    for candidate in \
        /dev/ttyACM0 \
        /dev/ttyACM1 \
        /dev/ttyUSB0 \
        /dev/ttyUSB1
    do
        if [[ -c "${candidate}" ]]; then
            UART_DEVICE="${candidate}"
            echo "Auto-detected UART device: ${UART_DEVICE}"
            return
        fi
    done

    echo "No UART device auto-detected."
    echo "UART results will be marked MANUAL."
}

uart_capture_available() {
    [[ -n "${UART_DEVICE}" ]] || return 1
    [[ -c "${UART_DEVICE}" ]] || return 1

    python3 - <<'PY' >/dev/null 2>&1
import serial
PY
}

capture_uart() {
    local output_file="$1"

    if ! uart_capture_available; then
        echo
        echo "UART capture window: ${WAIT_SECONDS} seconds"
        echo "Use the external UART logger."
        sleep "${WAIT_SECONDS}"
        : > "${output_file}"
        return 2
    fi

    echo
    echo "Capturing ${UART_DEVICE} at ${UART_BAUD} baud"
    echo "Capture duration: ${WAIT_SECONDS} seconds"

    UART_DEVICE="${UART_DEVICE}" \
    UART_BAUD="${UART_BAUD}" \
    WAIT_SECONDS="${WAIT_SECONDS}" \
    python3 - <<'PY' | tee "${output_file}"
import os
import sys
import time

import serial

device = os.environ["UART_DEVICE"]
baud = int(os.environ["UART_BAUD"])
duration = float(os.environ["WAIT_SECONDS"])

try:
    with serial.Serial(
        device,
        baudrate=baud,
        timeout=0.10,
        write_timeout=0.10,
    ) as uart:
        uart.reset_input_buffer()
        deadline = time.monotonic() + duration

        while time.monotonic() < deadline:
            data = uart.read(512)
            if data:
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()

except Exception as exc:
    print(f"UART_CAPTURE_ERROR: {exc}", file=sys.stderr)
    raise SystemExit(2)
PY
}

###############################################################################
# Reports
###############################################################################

csv_escape() {
    local value="$1"
    value="${value//\"/\"\"}"
    printf '"%s"' "${value}"
}

record_result() {
    local id="$1"
    local name="$2"
    local result="$3"
    local expected="$4"
    local evidence="$5"
    local uart_file="$6"
    local duration="$7"

    {
        printf '%s,' "${id}"
        csv_escape "${name}"
        printf ','
        csv_escape "${result}"
        printf ','
        csv_escape "${expected}"
        printf ','
        csv_escape "${evidence}"
        printf ','
        csv_escape "${uart_file}"
        printf ',%s\n' "${duration}"
    } >> "${CSV_REPORT}"

    {
        echo "| ${id} | ${name} | ${result} | ${duration}s |"
    } >> "${MD_REPORT}"

    case "${result}" in
        PASS)
            PASS_COUNT=$((PASS_COUNT + 1))
            ;;
        FAIL)
            FAIL_COUNT=$((FAIL_COUNT + 1))
            ;;
        OBSERVE)
            OBSERVE_COUNT=$((OBSERVE_COUNT + 1))
            ;;
        MANUAL)
            MANUAL_COUNT=$((MANUAL_COUNT + 1))
            ;;
        ERROR)
            ERROR_COUNT=$((ERROR_COUNT + 1))
            ;;
    esac
}

###############################################################################
# Test-state helpers
###############################################################################

restore_original_metadata_no_reset() {
    flash_write_no_reset \
        "${BACKUP_DIR}/metadata-a.bin" \
        "${METADATA_A_ADDRESS}"

    flash_write_no_reset \
        "${BACKUP_DIR}/metadata-b.bin" \
        "${METADATA_B_ADDRESS}"
}

invalidate_both_slots_no_reset() {
    flash_write_no_reset \
        "${CACHE_DIR}/erased-header.bin" \
        "${SLOT_A_ADDRESS}"

    flash_write_no_reset \
        "${CACHE_DIR}/erased-header.bin" \
        "${SLOT_B_ADDRESS}"
}

prepare_clean_test_state() {
    restore_original_metadata_no_reset
    invalidate_both_slots_no_reset
}

###############################################################################
# Test runner
###############################################################################

run_test() {
    local name="$1"
    local expected="$2"
    local required_regex="$3"
    local forbidden_regex="$4"
    local mode="${5:-automatic}"

    CURRENT_TEST=$((CURRENT_TEST + 1))

    local test_id
    printf -v test_id "%02d" "${CURRENT_TEST}"

    local uart_file="${UART_DIR}/test-${test_id}.log"
    local test_start
    local test_end
    local duration
    local capture_status=0
    local result
    local evidence

    test_start="$(date +%s)"

    section "TEST ${test_id}: ${name}"

    echo "Expected behaviour:"
    echo "  ${expected}"
    echo

    board_reset || true

    set +e
    capture_uart "${uart_file}"
    capture_status=$?
    set -e

    if (( capture_status == 2 )); then
        result="MANUAL"
        evidence="No automatic UART capture available"
    elif [[ "${mode}" == "observe" ]]; then
        result="OBSERVE"
        evidence="Behaviour recorded for policy analysis"
    elif [[ ! -s "${uart_file}" ]]; then
        result="FAIL"
        evidence="UART capture was empty"
    elif [[ -n "${required_regex}" ]] &&
         ! grep -Eiq "${required_regex}" "${uart_file}"; then
        result="FAIL"
        evidence="Required UART pattern not found: ${required_regex}"
    elif [[ -n "${forbidden_regex}" ]] &&
         grep -Eiq "${forbidden_regex}" "${uart_file}"; then
        result="FAIL"
        evidence="Forbidden UART pattern found: ${forbidden_regex}"
    else
        result="PASS"
        evidence="UART matched expected security behaviour"
    fi

    test_end="$(date +%s)"
    duration=$((test_end - test_start))

    echo
    echo "Test result: ${result}"
    echo "Evidence: ${evidence}"

    record_result \
        "${test_id}" \
        "${name}" \
        "${result}" \
        "${expected}" \
        "${evidence}" \
        "${uart_file}" \
        "${duration}"
}

###############################################################################
# Preconditions
###############################################################################

for command in \
    make \
    python3 \
    sha256sum \
    st-flash \
    arm-none-eabi-gcc \
    arm-none-eabi-objcopy \
    cmp \
    find \
    tee \
    grep
do
    require_command "${command}"
done

detect_uart_device

section "ENVIRONMENT"

date --iso-8601=seconds
uname -a
echo "Repository: ${ROOT}"
echo "Git commit:"
git -C "${ROOT}" rev-parse HEAD || true
echo
echo "Git status:"
git -C "${ROOT}" status --short || true
echo
st-flash --version || true
arm-none-eabi-gcc --version | head -n 1
python3 --version
echo "UART device: ${UART_DEVICE:-external/manual}"
echo "UART baud: ${UART_BAUD}"
echo "Wait seconds: ${WAIT_SECONDS}"
echo "Reset cycles: ${RESET_CYCLES}"

###############################################################################
# Report initialization
###############################################################################

cat > "${CSV_REPORT}" <<'CSV'
test_id,test_name,result,expected,evidence,uart_file,duration_seconds
CSV

cat > "${MD_REPORT}" <<EOF
# Secure Boot Slot-Policy Validation

- Date: $(date --iso-8601=seconds)
- Git commit: $(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)
- Layout: ${LAYOUT_PROFILE}
- UART: ${UART_DEVICE:-external/manual}
- Baud: ${UART_BAUD}

| Test | Name | Result | Duration |
|---:|---|---|---:|
EOF

###############################################################################
# Backup
###############################################################################

section "BACK UP ORIGINAL FLASH"

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

for required_backup in \
    "${BACKUP_DIR}/bootloader.bin" \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${BACKUP_DIR}/slot-a.bin" \
    "${BACKUP_DIR}/slot-b.bin"
do
    [[ -s "${required_backup}" ]] ||
        die "backup is empty: ${required_backup}"
done

ORIGINAL_STATE_CAPTURED=1

sha256sum "${BACKUP_DIR}"/*.bin |
    tee "${REPORT_DIR}/backup-sha256.txt"

if cmp -s \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${BACKUP_DIR}/metadata-b.bin"
then
    echo "Metadata copies are byte-identical."
else
    echo "Metadata copies differ."
fi

###############################################################################
# Build bootloader
###############################################################################

section "BUILD BOOTLOADER"

make -C "${BOOT_DIR}" clean all \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}"

[[ -s "${BOOT_BUILD_BIN}" ]] ||
    die "bootloader build output missing"

cp "${BOOT_BUILD_BIN}" \
   "${CACHE_DIR}/bootloader-under-test.bin"

sha256sum "${CACHE_DIR}/bootloader-under-test.bin"

###############################################################################
# Build and immediately cache Slot A
###############################################################################

section "BUILD AND CACHE SLOT-A PACKAGE"

make -C "${APP_DIR}" SLOT=a clean all verify-signed \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}" \
    SIGNING_SEED="${SIGNING_SEED}" \
    PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER}"

SLOT_A_BUILD_PACKAGE="${APP_BUILD_DIR}/exp066_research_platform_core_slot_a_update_v2.bin"

[[ -s "${SLOT_A_BUILD_PACKAGE}" ]] ||
    die "Slot-A update package missing"

cp "${SLOT_A_BUILD_PACKAGE}" \
   "${CACHE_DIR}/slot-a-valid.bin"

[[ -s "${CACHE_DIR}/slot-a-valid.bin" ]] ||
    die "cached Slot-A package missing"

sha256sum "${CACHE_DIR}/slot-a-valid.bin"

###############################################################################
# Build and immediately cache Slot B
###############################################################################

section "BUILD AND CACHE SLOT-B PACKAGE"

make -C "${APP_DIR}" SLOT=b clean all verify-signed \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}" \
    SIGNING_SEED="${SIGNING_SEED}" \
    PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER}"

SLOT_B_BUILD_PACKAGE="${APP_BUILD_DIR}/exp066_research_platform_core_slot_b_update_v2.bin"

[[ -s "${SLOT_B_BUILD_PACKAGE}" ]] ||
    die "Slot-B update package missing"

cp "${SLOT_B_BUILD_PACKAGE}" \
   "${CACHE_DIR}/slot-b-valid.bin"

[[ -s "${CACHE_DIR}/slot-b-valid.bin" ]] ||
    die "cached Slot-B package missing"

sha256sum "${CACHE_DIR}/slot-b-valid.bin"

###############################################################################
# Generate mutated and erased images
###############################################################################

section "GENERATE TEST IMAGES"

CACHE_DIR="${CACHE_DIR}" python3 - <<'PY'
from pathlib import Path
import hashlib
import json
import os
import struct

cache = Path(os.environ["CACHE_DIR"])

slot_a = (cache / "slot-a-valid.bin").read_bytes()
slot_b = (cache / "slot-b-valid.bin").read_bytes()

if len(slot_a) < 0x204 or len(slot_b) < 0x204:
    raise SystemExit("Signed update package is unexpectedly small")

def mutate(source: bytes, output: str, offset: int, mask: int) -> None:
    data = bytearray(source)
    data[offset] ^= mask
    (cache / output).write_bytes(data)

mutate(
    slot_a,
    "slot-a-bad-signature.bin",
    0x60,
    0x01,
)

mutate(
    slot_a,
    "slot-a-bad-payload.bin",
    0x220,
    0x01,
)

mutate(
    slot_b,
    "slot-b-bad-signature.bin",
    0x60,
    0x01,
)

mutate(
    slot_b,
    "slot-b-bad-payload.bin",
    0x220,
    0x01,
)

(cache / "erased-header.bin").write_bytes(b"\xFF" * 0x200)
(cache / "zero-header.bin").write_bytes(b"\x00" * 0x200)

(cache / "metadata-erased.bin").write_bytes(b"\xFF" * 0x4000)
(cache / "metadata-zero.bin").write_bytes(b"\x00" * 0x4000)

manifest = {}

for path in sorted(cache.glob("*.bin")):
    data = path.read_bytes()
    manifest[path.name] = {
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }

for slot_name in ("slot-a-valid.bin", "slot-b-valid.bin"):
    data = (cache / slot_name).read_bytes()
    fields = struct.unpack_from("<8I", data, 0)
    msp, reset = struct.unpack_from("<2I", data, 0x200)

    manifest[slot_name]["manifest"] = {
        "magic": f"0x{fields[0]:08X}",
        "header_version": fields[1],
        "image_version": fields[2],
        "vector_address": f"0x{fields[3]:08X}",
        "image_size": fields[4],
        "flags": f"0x{fields[5]:08X}",
        "reserved0": f"0x{fields[6]:08X}",
        "reserved1": f"0x{fields[7]:08X}",
        "initial_msp": f"0x{msp:08X}",
        "reset_vector": f"0x{reset:08X}",
    }

(cache / "image-manifest.json").write_text(
    json.dumps(manifest, indent=2) + "\n",
    encoding="utf-8",
)

print(json.dumps(manifest, indent=2))
PY

sha256sum "${CACHE_DIR}"/*.bin |
    tee "${REPORT_DIR}/test-image-sha256.txt"

###############################################################################
# Install bootloader under test
###############################################################################

section "INSTALL BOOTLOADER UNDER TEST"

flash_write_no_reset \
    "${CACHE_DIR}/bootloader-under-test.bin" \
    "${BOOT_ADDRESS}"

###############################################################################
# Test 1: baseline with original metadata and valid A
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

run_test \
    "Baseline: original metadata and valid Slot A" \
    "Confirmed Slot A verifies successfully and EXP066 starts." \
    "Slot policy[[:space:]]*=[[:space:]]*OK.*|Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Bootloader halted safely|Application will NOT be started"

###############################################################################
# Test 2: Slot A only
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

run_test \
    "Only Slot A valid" \
    "Slot A boots; invalid Slot B must not interfere." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Bootloader halted safely|Application will NOT be started"

###############################################################################
# Test 3: Slot B only
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-valid.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Only Slot B valid with original metadata" \
    "Observe whether policy allows Slot B fallback when confirmed Slot A is absent." \
    "" \
    "" \
    "observe"

###############################################################################
# Test 4: both valid
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-valid.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Both slots valid" \
    "Bootloader makes a deterministic metadata-driven slot decision." \
    "Slot decision[[:space:]]*=|Verification[[:space:]]*=[[:space:]]*OK" \
    "BAD MAGIC|BAD HEADER VERSION|PAYLOAD SHA512 MISMATCH|ED25519 SIGNATURE INVALID"

###############################################################################
# Test 5: A bad signature, B valid
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-bad-signature.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-valid.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Slot A bad signature, Slot B valid" \
    "Slot A must never execute; valid Slot B may boot only through defined fallback." \
    "ED25519 SIGNATURE INVALID|Verification[[:space:]]*=[[:space:]]*OK|Bootloader halted safely" \
    "" \
    "observe"

###############################################################################
# Test 6: A bad payload, B valid
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-bad-payload.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-valid.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Slot A payload corrupt, Slot B valid" \
    "Corrupt Slot A must never execute; policy may fall back to verified Slot B." \
    "PAYLOAD SHA512 MISMATCH|Verification[[:space:]]*=[[:space:]]*OK|Bootloader halted safely" \
    "" \
    "observe"

###############################################################################
# Test 7: A valid, B bad signature
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-bad-signature.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Slot A valid, Slot B bad signature" \
    "Valid confirmed Slot A boots; invalid unused Slot B must not interfere." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Test 8: A valid, B bad payload
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-bad-payload.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Slot A valid, Slot B payload corrupt" \
    "Valid confirmed Slot A boots; corrupt unused Slot B must not interfere." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Test 9: both bad signatures
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-bad-signature.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-bad-signature.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Both slots have invalid signatures" \
    "No application may execute; bootloader must halt safely or enter recovery." \
    "ED25519 SIGNATURE INVALID|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

###############################################################################
# Test 10: both payloads corrupt
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-bad-payload.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/slot-b-bad-payload.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Both slots have corrupt payloads" \
    "No application may execute; both hashes must be rejected." \
    "PAYLOAD SHA512 MISMATCH|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

###############################################################################
# Test 11: both headers erased
###############################################################################

prepare_clean_test_state

run_test \
    "Both slot headers erased" \
    "No application may execute; bootloader must report no bootable slot." \
    "BAD MAGIC|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

###############################################################################
# Test 12: both headers zero
###############################################################################

restore_original_metadata_no_reset

flash_write_no_reset \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Both slot headers zeroed" \
    "No application may execute; malformed manifests must be rejected." \
    "BAD MAGIC|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

###############################################################################
# Test 13: Metadata A erased, B intact
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_A_ADDRESS}"

run_test \
    "Metadata Copy A erased, Copy B intact" \
    "Bootloader should recover from valid Metadata Copy B." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Test 14: Metadata B erased, A intact
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Metadata Copy B erased, Copy A intact" \
    "Bootloader should recover from valid Metadata Copy A." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Test 15: Metadata A zeroed, B intact
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_A_ADDRESS}"

run_test \
    "Metadata Copy A zeroed, Copy B intact" \
    "Bootloader should reject Copy A and recover from Copy B." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Test 16: Metadata B zeroed, A intact
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Metadata Copy B zeroed, Copy A intact" \
    "Bootloader should reject Copy B and recover from Copy A." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Test 17: both metadata copies erased
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Both metadata copies erased" \
    "Bootloader must use a defined default/recovery policy or halt safely." \
    "" \
    "" \
    "observe"

###############################################################################
# Test 18: both metadata copies zeroed
###############################################################################

prepare_clean_test_state

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Both metadata copies zeroed" \
    "Bootloader must reject malformed metadata and never bypass verification." \
    "" \
    "" \
    "observe"

###############################################################################
# Test 19: invalid metadata and invalid slots
###############################################################################

flash_write_no_reset \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_B_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_A_ADDRESS}"

flash_write_no_reset \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Invalid metadata and invalid slots" \
    "Worst-case persistent corruption must end in safe halt or recovery." \
    "NO BOOTABLE SLOT|BAD MAGIC|Bootloader halted safely|Application will NOT be started|Recovery" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

###############################################################################
# Test 20: recovery from restored metadata after total corruption
###############################################################################

restore_original_metadata_no_reset
invalidate_both_slots_no_reset

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

run_test \
    "Recovery after restoring valid metadata" \
    "Restored metadata and valid Slot A must return system to normal boot." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Reset stability tests
###############################################################################

section "RESET STABILITY PREPARATION"

restore_original_metadata_no_reset
invalidate_both_slots_no_reset

flash_write_no_reset \
    "${CACHE_DIR}/slot-a-valid.bin" \
    "${SLOT_A_ADDRESS}"

for cycle in $(seq 1 "${RESET_CYCLES}"); do
    run_test \
        "Valid boot reset cycle ${cycle}/${RESET_CYCLES}" \
        "Every reset must verify and start the same valid application." \
        "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
        "Application will NOT be started|Bootloader halted safely"
done

###############################################################################
# Restore original flash
###############################################################################

restore_original_flash "normal completion"

###############################################################################
# Verify restored flash by reading it back
###############################################################################

section "VERIFY RESTORED FLASH"

flash_read \
    "${RESULT_DIR}/restored-bootloader.bin" \
    "${BOOT_ADDRESS}" \
    "${BOOT_BACKUP_SIZE}"

flash_read \
    "${RESULT_DIR}/restored-metadata-a.bin" \
    "${METADATA_A_ADDRESS}" \
    "${METADATA_COPY_SIZE}"

flash_read \
    "${RESULT_DIR}/restored-metadata-b.bin" \
    "${METADATA_B_ADDRESS}" \
    "${METADATA_COPY_SIZE}"

flash_read \
    "${RESULT_DIR}/restored-slot-a.bin" \
    "${SLOT_A_ADDRESS}" \
    "${SLOT_SIZE}"

flash_read \
    "${RESULT_DIR}/restored-slot-b.bin" \
    "${SLOT_B_ADDRESS}" \
    "${SLOT_SIZE}"

RESTORE_VERIFY_FAILURES=0

verify_restored_file() {
    local original="$1"
    local restored="$2"
    local label="$3"

    if cmp -s "${original}" "${restored}"; then
        echo "[PASS] Restore verification: ${label}"
    else
        echo "[FAIL] Restore verification: ${label}"
        RESTORE_VERIFY_FAILURES=$((RESTORE_VERIFY_FAILURES + 1))
    fi
}

verify_restored_file \
    "${BACKUP_DIR}/bootloader.bin" \
    "${RESULT_DIR}/restored-bootloader.bin" \
    "bootloader"

verify_restored_file \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${RESULT_DIR}/restored-metadata-a.bin" \
    "metadata A"

verify_restored_file \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${RESULT_DIR}/restored-metadata-b.bin" \
    "metadata B"

verify_restored_file \
    "${BACKUP_DIR}/slot-a.bin" \
    "${RESULT_DIR}/restored-slot-a.bin" \
    "Slot A"

verify_restored_file \
    "${BACKUP_DIR}/slot-b.bin" \
    "${RESULT_DIR}/restored-slot-b.bin" \
    "Slot B"

###############################################################################
# Final report
###############################################################################

END_EPOCH="$(date +%s)"
ELAPSED_SECONDS=$((END_EPOCH - START_EPOCH))

{
    echo
    echo "## Summary"
    echo
    echo "- Tests executed: ${CURRENT_TEST}"
    echo "- PASS: ${PASS_COUNT}"
    echo "- FAIL: ${FAIL_COUNT}"
    echo "- OBSERVE: ${OBSERVE_COUNT}"
    echo "- MANUAL: ${MANUAL_COUNT}"
    echo "- ERROR: ${ERROR_COUNT}"
    echo "- Restore verification failures: ${RESTORE_VERIFY_FAILURES}"
    echo "- Elapsed seconds: ${ELAPSED_SECONDS}"
    echo
    echo "## Result interpretation"
    echo
    echo "- PASS: UART matched the defined expected security behaviour."
    echo "- FAIL: Required output was absent or forbidden output appeared."
    echo "- OBSERVE: Policy-dependent behaviour was captured for analysis."
    echo "- MANUAL: No integrated UART capture was available."
} >> "${MD_REPORT}"

{
    echo "STM32 Secure Boot Slot-Policy Test Monster v2"
    echo "Date: $(date --iso-8601=seconds)"
    echo "Git commit: $(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo
    echo "Tests executed: ${CURRENT_TEST}"
    echo "PASS: ${PASS_COUNT}"
    echo "FAIL: ${FAIL_COUNT}"
    echo "OBSERVE: ${OBSERVE_COUNT}"
    echo "MANUAL: ${MANUAL_COUNT}"
    echo "ERROR: ${ERROR_COUNT}"
    echo
    echo "Restore verification failures: ${RESTORE_VERIFY_FAILURES}"
    echo "Elapsed seconds: ${ELAPSED_SECONDS}"
    echo
    echo "Original flash state restored: ${RESTORE_COMPLETED}"
    echo
    echo "Artifacts:"
    echo "  Terminal log: ${TERMINAL_LOG}"
    echo "  CSV report: ${CSV_REPORT}"
    echo "  Markdown report: ${MD_REPORT}"
    echo "  UART logs: ${UART_DIR}"
    echo "  Flash backup: ${BACKUP_DIR}"
    echo "  Image cache: ${CACHE_DIR}"
} | tee "${SUMMARY}"

section "FINAL RESULT"

cat "${SUMMARY}"

if (( FAIL_COUNT > 0 ||
      ERROR_COUNT > 0 ||
      RESTORE_VERIFY_FAILURES > 0 )); then
    echo
    echo "TEST SUITE COMPLETED WITH FAILURES."
    exit 1
fi

echo
echo "TEST SUITE COMPLETED WITHOUT AUTOMATIC FAILURES."
