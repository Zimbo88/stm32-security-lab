#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# STM32F429 Secure-Boot Slot-Policy Test Suite
#
# Automatic UART example:
#
#   UART_DEVICE=/dev/ttyUSB0 \
#   WAIT_SECONDS=5 \
#   RESET_CYCLES=10 \
#   ./run_slot_policy_clean_suite.sh
#
# Without UART_DEVICE:
#   Keep an external UART terminal/logger open.
#   Tests are recorded as MANUAL instead of being guessed as PASS.
###############################################################################

ROOT="${HOME}/stm32-security-lab"

BOOT_DIR="${ROOT}/firmware/exp045_bootloader_v2"
APP_DIR="${ROOT}/firmware/exp066_research_platform_core"

BOOT_BUILD_BIN="${BOOT_DIR}/build/exp045_bootloader_v2.bin"
APP_BUILD_DIR="${APP_DIR}/build"

LAYOUT_PROFILE="${LAYOUT_PROFILE:-stm32f429_1m}"
SIGNING_SEED="${SIGNING_SEED:-../exp065_signed_app/keys/firmware_signing_seed.bin}"
PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER:-../exp045_bootloader_v2/src/firmware_public_key.h}"

BOOT_ADDRESS="0x08000000"
METADATA_A_ADDRESS="0x08008000"
METADATA_B_ADDRESS="0x0800C000"
SLOT_A_ADDRESS="0x08020000"
SLOT_B_ADDRESS="0x08080000"

BOOT_SIZE="0x8000"
METADATA_SIZE="0x4000"
SLOT_SIZE="0x60000"

UART_DEVICE="${UART_DEVICE:-}"
UART_BAUD="${UART_BAUD:-115200}"
WAIT_SECONDS="${WAIT_SECONDS:-5}"
RESET_CYCLES="${RESET_CYCLES:-10}"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${ROOT}/test-results/slot-policy-clean-${RUN_ID}"

BACKUP_DIR="${RESULT_DIR}/flash-backup"
CACHE_DIR="${RESULT_DIR}/image-cache"
UART_DIR="${RESULT_DIR}/uart"
REPORT_DIR="${RESULT_DIR}/reports"
READBACK_DIR="${RESULT_DIR}/restore-readback"

TERMINAL_LOG="${RESULT_DIR}/terminal.log"
CSV_REPORT="${REPORT_DIR}/results.csv"
MD_REPORT="${REPORT_DIR}/results.md"
SUMMARY_REPORT="${REPORT_DIR}/summary.txt"

mkdir -p \
    "${BACKUP_DIR}" \
    "${CACHE_DIR}" \
    "${UART_DIR}" \
    "${REPORT_DIR}" \
    "${READBACK_DIR}"

exec > >(tee -a "${TERMINAL_LOG}") 2>&1

BACKUP_COMPLETE=0
RESTORE_COMPLETE=0

TEST_COUNT=0
PASS_COUNT=0
FAIL_COUNT=0
MANUAL_COUNT=0
OBSERVE_COUNT=0
ERROR_COUNT=0

SUITE_START="$(date +%s)"

###############################################################################
# General helpers
###############################################################################

section() {
    printf '\n'
    printf '%s\n' \
        '================================================================'
    printf '%s\n' "$1"
    printf '%s\n' \
        '================================================================'
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 ||
        die "Required command not found: $1"
}

require_file() {
    [[ -s "$1" ]] ||
        die "Required file missing or empty: $1"
}

###############################################################################
# Flash helpers
###############################################################################

flash_read() {
    local output="$1"
    local address="$2"
    local size="$3"

    echo "READ"
    echo "  Address: ${address}"
    echo "  Size:    ${size}"
    echo "  Output:  ${output}"

    st-flash read "${output}" "${address}" "${size}"
    require_file "${output}"
}

flash_write() {
    local input="$1"
    local address="$2"

    require_file "${input}"

    echo "WRITE"
    echo "  Input:   ${input}"
    echo "  Address: ${address}"

    st-flash write "${input}" "${address}"
}

board_reset() {
    echo "RESET"
    st-flash reset
}

###############################################################################
# Restore handling
###############################################################################

restore_original_flash() {
    local reason="${1:-unspecified}"

    if (( BACKUP_COMPLETE == 0 )); then
        echo "Restore skipped: no complete backup exists."
        return 0
    fi

    if (( RESTORE_COMPLETE == 1 )); then
        return 0
    fi

    section "RESTORE ORIGINAL FLASH: ${reason}"

    set +e

    local status=0

    flash_write \
        "${BACKUP_DIR}/bootloader.bin" \
        "${BOOT_ADDRESS}" || status=1

    flash_write \
        "${BACKUP_DIR}/metadata-a.bin" \
        "${METADATA_A_ADDRESS}" || status=1

    flash_write \
        "${BACKUP_DIR}/metadata-b.bin" \
        "${METADATA_B_ADDRESS}" || status=1

    flash_write \
        "${BACKUP_DIR}/slot-a.bin" \
        "${SLOT_A_ADDRESS}" || status=1

    flash_write \
        "${BACKUP_DIR}/slot-b.bin" \
        "${SLOT_B_ADDRESS}" || status=1

    board_reset || status=1

    if (( status == 0 )); then
        RESTORE_COMPLETE=1
        echo "Original flash contents restored."
    else
        echo "WARNING: Restore encountered an error."
    fi

    set -e
    return "${status}"
}

exit_handler() {
    local status=$?

    trap - EXIT INT TERM

    if (( BACKUP_COMPLETE == 1 && RESTORE_COMPLETE == 0 )); then
        restore_original_flash "automatic exit handler" || true
    fi

    exit "${status}"
}

interrupt_handler() {
    echo
    echo "Interrupt received."
    exit 130
}

trap exit_handler EXIT
trap interrupt_handler INT TERM

###############################################################################
# UART capture
###############################################################################

uart_available() {
    [[ -n "${UART_DEVICE}" ]] || return 1
    [[ -c "${UART_DEVICE}" ]] || return 1

    python3 - <<'PY' >/dev/null 2>&1
import serial
PY
}

capture_boot_uart() {
    local output="$1"

    if ! uart_available; then
        echo "Automatic UART capture unavailable."
        echo "Waiting ${WAIT_SECONDS} seconds for external UART logging."

        board_reset || true
        sleep "${WAIT_SECONDS}"

        : > "${output}"
        return 2
    fi

    echo "Capturing UART:"
    echo "  Device: ${UART_DEVICE}"
    echo "  Baud:   ${UART_BAUD}"
    echo "  Time:   ${WAIT_SECONDS}s"

    UART_DEVICE="${UART_DEVICE}" \
    UART_BAUD="${UART_BAUD}" \
    WAIT_SECONDS="${WAIT_SECONDS}" \
    python3 - "${output}" <<'PY'
from pathlib import Path
import os
import subprocess
import sys
import time

import serial

output = Path(sys.argv[1])
device = os.environ["UART_DEVICE"]
baud = int(os.environ["UART_BAUD"])
duration = float(os.environ["WAIT_SECONDS"])

collected = bytearray()

try:
    with serial.Serial(
        device,
        baudrate=baud,
        timeout=0.05,
        write_timeout=0.5,
    ) as uart:
        uart.reset_input_buffer()

        reset = subprocess.run(
            ["st-flash", "reset"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )

        if reset.returncode != 0:
            print(reset.stdout, file=sys.stderr)
            raise SystemExit(3)

        deadline = time.monotonic() + duration

        while time.monotonic() < deadline:
            chunk = uart.read(512)
            if chunk:
                collected.extend(chunk)
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()

except Exception as exc:
    print(f"UART capture failed: {exc}", file=sys.stderr)
    raise SystemExit(2)

output.write_bytes(collected)
PY
}

###############################################################################
# Reporting
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

    printf '| %s | %s | %s | %ss |\n' \
        "${id}" \
        "${name}" \
        "${result}" \
        "${duration}" >> "${MD_REPORT}"

    case "${result}" in
        PASS)
            PASS_COUNT=$((PASS_COUNT + 1))
            ;;
        FAIL)
            FAIL_COUNT=$((FAIL_COUNT + 1))
            ;;
        MANUAL)
            MANUAL_COUNT=$((MANUAL_COUNT + 1))
            ;;
        OBSERVE)
            OBSERVE_COUNT=$((OBSERVE_COUNT + 1))
            ;;
        ERROR)
            ERROR_COUNT=$((ERROR_COUNT + 1))
            ;;
    esac
}

###############################################################################
# Test-state helpers
###############################################################################

restore_metadata() {
    flash_write \
        "${BACKUP_DIR}/metadata-a.bin" \
        "${METADATA_A_ADDRESS}"

    flash_write \
        "${BACKUP_DIR}/metadata-b.bin" \
        "${METADATA_B_ADDRESS}"
}

invalidate_slot_a() {
    flash_write \
        "${CACHE_DIR}/erased-header.bin" \
        "${SLOT_A_ADDRESS}"
}

invalidate_slot_b() {
    flash_write \
        "${CACHE_DIR}/erased-header.bin" \
        "${SLOT_B_ADDRESS}"
}

prepare_default_state() {
    restore_metadata
    invalidate_slot_a
    invalidate_slot_b
}

install_slot_a_valid() {
    flash_write \
        "${CACHE_DIR}/slot-a-valid.bin" \
        "${SLOT_A_ADDRESS}"
}

install_slot_b_valid() {
    flash_write \
        "${CACHE_DIR}/slot-b-valid.bin" \
        "${SLOT_B_ADDRESS}"
}

###############################################################################
# Test runner
###############################################################################

run_test() {
    local name="$1"
    local expected="$2"
    local required_regex="${3:-}"
    local forbidden_regex="${4:-}"
    local mode="${5:-automatic}"

    TEST_COUNT=$((TEST_COUNT + 1))

    local id
    printf -v id '%02d' "${TEST_COUNT}"

    local uart_file="${UART_DIR}/test-${id}.log"
    local start
    local end
    local duration
    local capture_status
    local result
    local evidence

    section "TEST ${id}: ${name}"

    echo "Expected:"
    echo "  ${expected}"

    start="$(date +%s)"

    set +e
    capture_boot_uart "${uart_file}"
    capture_status=$?
    set -e

    if (( capture_status == 2 )); then
        result="MANUAL"
        evidence="No integrated UART capture; inspect external UART log"
    elif (( capture_status != 0 )); then
        result="ERROR"
        evidence="UART capture or reset failed with status ${capture_status}"
    elif [[ "${mode}" == "observe" ]]; then
        result="OBSERVE"
        evidence="Policy-dependent behaviour captured without forced verdict"
    elif [[ ! -s "${uart_file}" ]]; then
        result="FAIL"
        evidence="UART log is empty"
    elif [[ -n "${required_regex}" ]] &&
         ! grep -Eiq "${required_regex}" "${uart_file}"; then
        result="FAIL"
        evidence="Required pattern missing: ${required_regex}"
    elif [[ -n "${forbidden_regex}" ]] &&
         grep -Eiq "${forbidden_regex}" "${uart_file}"; then
        result="FAIL"
        evidence="Forbidden pattern present: ${forbidden_regex}"
    else
        result="PASS"
        evidence="UART output matched expected behaviour"
    fi

    end="$(date +%s)"
    duration=$((end - start))

    echo
    echo "Result:   ${result}"
    echo "Evidence: ${evidence}"

    record_result \
        "${id}" \
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
    cmp \
    grep \
    st-flash \
    arm-none-eabi-gcc \
    arm-none-eabi-objcopy
do
    require_command "${command}"
done

section "ENVIRONMENT"

date --iso-8601=seconds
uname -a
echo
echo "Git commit:"
git -C "${ROOT}" rev-parse HEAD || true
echo
echo "Git status:"
git -C "${ROOT}" status --short || true
echo
st-flash --version || true
arm-none-eabi-gcc --version | head -n 1
python3 --version
echo
echo "UART device: ${UART_DEVICE:-external/manual}"
echo "UART baud: ${UART_BAUD}"
echo "Wait seconds: ${WAIT_SECONDS}"
echo "Reset cycles: ${RESET_CYCLES}"

###############################################################################
# Initialize reports
###############################################################################

cat > "${CSV_REPORT}" <<'CSV'
test_id,test_name,result,expected,evidence,uart_file,duration_seconds
CSV

cat > "${MD_REPORT}" <<EOF
# STM32 Secure-Boot Slot-Policy Test Report

- Date: $(date --iso-8601=seconds)
- Git commit: $(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)
- Layout profile: ${LAYOUT_PROFILE}
- UART device: ${UART_DEVICE:-external/manual}
- UART baud: ${UART_BAUD}

| Test | Name | Result | Duration |
|---:|---|---|---:|
EOF

###############################################################################
# Backup original flash
###############################################################################

section "BACKUP ORIGINAL FLASH"

flash_read \
    "${BACKUP_DIR}/bootloader.bin" \
    "${BOOT_ADDRESS}" \
    "${BOOT_SIZE}"

flash_read \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${METADATA_A_ADDRESS}" \
    "${METADATA_SIZE}"

flash_read \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${METADATA_B_ADDRESS}" \
    "${METADATA_SIZE}"

flash_read \
    "${BACKUP_DIR}/slot-a.bin" \
    "${SLOT_A_ADDRESS}" \
    "${SLOT_SIZE}"

flash_read \
    "${BACKUP_DIR}/slot-b.bin" \
    "${SLOT_B_ADDRESS}" \
    "${SLOT_SIZE}"

BACKUP_COMPLETE=1

sha256sum "${BACKUP_DIR}"/*.bin |
    tee "${REPORT_DIR}/backup-sha256.txt"

if cmp -s \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${BACKUP_DIR}/metadata-b.bin"
then
    echo "Metadata A and B are byte-identical."
else
    echo "Metadata A and B differ."
fi

###############################################################################
# Build bootloader
###############################################################################

section "BUILD BOOTLOADER"

make -C "${BOOT_DIR}" clean all \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}"

require_file "${BOOT_BUILD_BIN}"

cp "${BOOT_BUILD_BIN}" \
    "${CACHE_DIR}/bootloader.bin"

require_file "${CACHE_DIR}/bootloader.bin"

###############################################################################
# Build and cache Slot A immediately
###############################################################################

section "BUILD AND CACHE SLOT A"

make -C "${APP_DIR}" SLOT=a clean all verify-signed \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}" \
    SIGNING_SEED="${SIGNING_SEED}" \
    PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER}"

SLOT_A_BUILD="${APP_BUILD_DIR}/exp066_research_platform_core_slot_a_update_v2.bin"

require_file "${SLOT_A_BUILD}"

cp "${SLOT_A_BUILD}" \
    "${CACHE_DIR}/slot-a-valid.bin"

require_file "${CACHE_DIR}/slot-a-valid.bin"

echo "Cached Slot A before building Slot B:"
sha256sum "${CACHE_DIR}/slot-a-valid.bin"

###############################################################################
# Build and cache Slot B immediately
###############################################################################

section "BUILD AND CACHE SLOT B"

make -C "${APP_DIR}" SLOT=b clean all verify-signed \
    LAYOUT_PROFILE="${LAYOUT_PROFILE}" \
    SIGNING_SEED="${SIGNING_SEED}" \
    PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER}"

SLOT_B_BUILD="${APP_BUILD_DIR}/exp066_research_platform_core_slot_b_update_v2.bin"

require_file "${SLOT_B_BUILD}"

cp "${SLOT_B_BUILD}" \
    "${CACHE_DIR}/slot-b-valid.bin"

require_file "${CACHE_DIR}/slot-b-valid.bin"

echo "Cached Slot B:"
sha256sum "${CACHE_DIR}/slot-b-valid.bin"

echo "Rechecking cached Slot A after Slot-B clean build:"
require_file "${CACHE_DIR}/slot-a-valid.bin"
sha256sum "${CACHE_DIR}/slot-a-valid.bin"

###############################################################################
# Generate corrupted images
###############################################################################

section "GENERATE TEST IMAGES"

CACHE_DIR="${CACHE_DIR}" python3 - <<'PY'
from pathlib import Path
import hashlib
import json
import os
import struct

cache = Path(os.environ["CACHE_DIR"])

slot_a = bytearray((cache / "slot-a-valid.bin").read_bytes())
slot_b = bytearray((cache / "slot-b-valid.bin").read_bytes())

if len(slot_a) < 0x240:
    raise SystemExit("Slot-A package is unexpectedly small")

if len(slot_b) < 0x240:
    raise SystemExit("Slot-B package is unexpectedly small")

def write_mutation(
    original: bytearray,
    output_name: str,
    offset: int,
    xor_mask: int,
) -> None:
    mutated = bytearray(original)
    mutated[offset] ^= xor_mask
    (cache / output_name).write_bytes(mutated)

write_mutation(
    slot_a,
    "slot-a-bad-signature.bin",
    0x60,
    0x01,
)

write_mutation(
    slot_a,
    "slot-a-bad-payload.bin",
    0x220,
    0x01,
)

write_mutation(
    slot_b,
    "slot-b-bad-signature.bin",
    0x60,
    0x01,
)

write_mutation(
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

for name in ("slot-a-valid.bin", "slot-b-valid.bin"):
    data = (cache / name).read_bytes()

    if len(data) >= 0x208:
        values = struct.unpack_from("<8I", data, 0)
        initial_msp, reset_vector = struct.unpack_from("<2I", data, 0x200)

        manifest[name]["parsed_header"] = {
            "magic": f"0x{values[0]:08X}",
            "header_version": values[1],
            "image_version": values[2],
            "vector_address": f"0x{values[3]:08X}",
            "image_size": values[4],
            "flags": f"0x{values[5]:08X}",
            "initial_msp": f"0x{initial_msp:08X}",
            "reset_vector": f"0x{reset_vector:08X}",
        }

(cache / "manifest.json").write_text(
    json.dumps(manifest, indent=2) + "\n",
    encoding="utf-8",
)

print(json.dumps(manifest, indent=2))
PY

sha256sum "${CACHE_DIR}"/*.bin |
    tee "${REPORT_DIR}/image-sha256.txt"

###############################################################################
# Install bootloader under test
###############################################################################

section "INSTALL BOOTLOADER UNDER TEST"

flash_write \
    "${CACHE_DIR}/bootloader.bin" \
    "${BOOT_ADDRESS}"

###############################################################################
# Slot tests
###############################################################################

prepare_default_state
install_slot_a_valid

run_test \
    "Baseline: valid Slot A" \
    "Confirmed Slot A verifies and EXP066 starts." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state
install_slot_a_valid

run_test \
    "Only Slot A valid" \
    "Slot A boots while Slot B remains invalid." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state
install_slot_b_valid

run_test \
    "Only Slot B valid with original metadata" \
    "Record whether the original confirmed-slot policy permits Slot-B fallback." \
    "" \
    "" \
    "observe"

prepare_default_state
install_slot_a_valid
install_slot_b_valid

run_test \
    "Both slots valid" \
    "Bootloader makes a deterministic metadata-driven decision." \
    "Slot decision[[:space:]]*=|Verification[[:space:]]*=[[:space:]]*OK" \
    "BAD MAGIC|BAD HEADER VERSION|PAYLOAD SHA512 MISMATCH|ED25519 SIGNATURE INVALID"

prepare_default_state

flash_write \
    "${CACHE_DIR}/slot-a-bad-signature.bin" \
    "${SLOT_A_ADDRESS}"

install_slot_b_valid

run_test \
    "Slot A bad signature, Slot B valid" \
    "Slot A must not execute; any Slot-B fallback must verify Slot B first." \
    "" \
    "Jumping to application.*ED25519 SIGNATURE INVALID" \
    "observe"

prepare_default_state

flash_write \
    "${CACHE_DIR}/slot-a-bad-payload.bin" \
    "${SLOT_A_ADDRESS}"

install_slot_b_valid

run_test \
    "Slot A payload corrupt, Slot B valid" \
    "Corrupt Slot A must not execute; fallback behaviour is recorded." \
    "" \
    "" \
    "observe"

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/slot-b-bad-signature.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Slot A valid, Slot B bad signature" \
    "Valid confirmed Slot A should boot normally." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/slot-b-bad-payload.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Slot A valid, Slot B payload corrupt" \
    "Valid confirmed Slot A should boot normally." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state

flash_write \
    "${CACHE_DIR}/slot-a-bad-signature.bin" \
    "${SLOT_A_ADDRESS}"

flash_write \
    "${CACHE_DIR}/slot-b-bad-signature.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Both signatures invalid" \
    "No application may execute." \
    "ED25519 SIGNATURE INVALID|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

prepare_default_state

flash_write \
    "${CACHE_DIR}/slot-a-bad-payload.bin" \
    "${SLOT_A_ADDRESS}"

flash_write \
    "${CACHE_DIR}/slot-b-bad-payload.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Both payloads corrupt" \
    "No application may execute." \
    "PAYLOAD SHA512 MISMATCH|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

prepare_default_state

run_test \
    "Both slot headers erased" \
    "No application may execute." \
    "BAD MAGIC|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

restore_metadata

flash_write \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_A_ADDRESS}"

flash_write \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Both slot headers zeroed" \
    "Malformed manifests must be rejected." \
    "BAD MAGIC|NO BOOTABLE SLOT|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

###############################################################################
# Metadata redundancy tests
###############################################################################

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_A_ADDRESS}"

run_test \
    "Metadata A erased, Metadata B intact" \
    "Bootloader should recover using Metadata B." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Metadata B erased, Metadata A intact" \
    "Bootloader should recover using Metadata A." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_A_ADDRESS}"

run_test \
    "Metadata A zeroed, Metadata B intact" \
    "Malformed Metadata A should be rejected; Metadata B remains usable." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Metadata B zeroed, Metadata A intact" \
    "Malformed Metadata B should be rejected; Metadata A remains usable." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_A_ADDRESS}"

flash_write \
    "${CACHE_DIR}/metadata-erased.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Both metadata copies erased" \
    "Record defined default, recovery or safe-halt behaviour." \
    "" \
    "" \
    "observe"

prepare_default_state
install_slot_a_valid

flash_write \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_A_ADDRESS}"

flash_write \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_B_ADDRESS}"

run_test \
    "Both metadata copies zeroed" \
    "Malformed metadata must never bypass image verification." \
    "" \
    "" \
    "observe"

flash_write \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_A_ADDRESS}"

flash_write \
    "${CACHE_DIR}/metadata-zero.bin" \
    "${METADATA_B_ADDRESS}"

flash_write \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_A_ADDRESS}"

flash_write \
    "${CACHE_DIR}/zero-header.bin" \
    "${SLOT_B_ADDRESS}"

run_test \
    "Invalid metadata and invalid slots" \
    "Worst-case corruption must result in recovery or safe halt." \
    "BAD MAGIC|NO BOOTABLE SLOT|Recovery|Bootloader halted safely|Application will NOT be started" \
    "EXP066 RESEARCH PLATFORM|Jumping to application"

###############################################################################
# Recovery to valid state
###############################################################################

prepare_default_state
install_slot_a_valid

run_test \
    "Return to valid state after corruption tests" \
    "Restored metadata and valid Slot A boot normally." \
    "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
    "Application will NOT be started|Bootloader halted safely"

###############################################################################
# Reset stability
###############################################################################

prepare_default_state
install_slot_a_valid

for cycle in $(seq 1 "${RESET_CYCLES}"); do
    run_test \
        "Valid reset cycle ${cycle}/${RESET_CYCLES}" \
        "Every reset verifies and starts the valid application." \
        "Verification[[:space:]]*=[[:space:]]*OK|EXP066 RESEARCH PLATFORM" \
        "Application will NOT be started|Bootloader halted safely"
done

###############################################################################
# Restore original flash
###############################################################################

restore_original_flash "normal suite completion"

###############################################################################
# Verify exact restore
###############################################################################

section "VERIFY RESTORE BY FULL READBACK"

flash_read \
    "${READBACK_DIR}/bootloader.bin" \
    "${BOOT_ADDRESS}" \
    "${BOOT_SIZE}"

flash_read \
    "${READBACK_DIR}/metadata-a.bin" \
    "${METADATA_A_ADDRESS}" \
    "${METADATA_SIZE}"

flash_read \
    "${READBACK_DIR}/metadata-b.bin" \
    "${METADATA_B_ADDRESS}" \
    "${METADATA_SIZE}"

flash_read \
    "${READBACK_DIR}/slot-a.bin" \
    "${SLOT_A_ADDRESS}" \
    "${SLOT_SIZE}"

flash_read \
    "${READBACK_DIR}/slot-b.bin" \
    "${SLOT_B_ADDRESS}" \
    "${SLOT_SIZE}"

RESTORE_FAILURES=0

verify_restore() {
    local original="$1"
    local restored="$2"
    local label="$3"

    if cmp -s "${original}" "${restored}"; then
        echo "[PASS] ${label}"
    else
        echo "[FAIL] ${label}"
        RESTORE_FAILURES=$((RESTORE_FAILURES + 1))
    fi
}

verify_restore \
    "${BACKUP_DIR}/bootloader.bin" \
    "${READBACK_DIR}/bootloader.bin" \
    "Bootloader restore"

verify_restore \
    "${BACKUP_DIR}/metadata-a.bin" \
    "${READBACK_DIR}/metadata-a.bin" \
    "Metadata A restore"

verify_restore \
    "${BACKUP_DIR}/metadata-b.bin" \
    "${READBACK_DIR}/metadata-b.bin" \
    "Metadata B restore"

verify_restore \
    "${BACKUP_DIR}/slot-a.bin" \
    "${READBACK_DIR}/slot-a.bin" \
    "Slot A restore"

verify_restore \
    "${BACKUP_DIR}/slot-b.bin" \
    "${READBACK_DIR}/slot-b.bin" \
    "Slot B restore"

###############################################################################
# Final report
###############################################################################

SUITE_END="$(date +%s)"
ELAPSED=$((SUITE_END - SUITE_START))

{
    echo
    echo "## Summary"
    echo
    echo "- Tests: ${TEST_COUNT}"
    echo "- PASS: ${PASS_COUNT}"
    echo "- FAIL: ${FAIL_COUNT}"
    echo "- MANUAL: ${MANUAL_COUNT}"
    echo "- OBSERVE: ${OBSERVE_COUNT}"
    echo "- ERROR: ${ERROR_COUNT}"
    echo "- Restore failures: ${RESTORE_FAILURES}"
    echo "- Elapsed seconds: ${ELAPSED}"
    echo
    echo "OBSERVE means that the result depends on the configured slot policy."
    echo "MANUAL means that no integrated UART device was configured."
} >> "${MD_REPORT}"

{
    echo "STM32 Secure-Boot Slot-Policy Test Suite"
    echo
    echo "Date: $(date --iso-8601=seconds)"
    echo "Git commit: $(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo
    echo "Tests: ${TEST_COUNT}"
    echo "PASS: ${PASS_COUNT}"
    echo "FAIL: ${FAIL_COUNT}"
    echo "MANUAL: ${MANUAL_COUNT}"
    echo "OBSERVE: ${OBSERVE_COUNT}"
    echo "ERROR: ${ERROR_COUNT}"
    echo
    echo "Restore failures: ${RESTORE_FAILURES}"
    echo "Restore complete: ${RESTORE_COMPLETE}"
    echo "Elapsed seconds: ${ELAPSED}"
    echo
    echo "Terminal log:"
    echo "  ${TERMINAL_LOG}"
    echo
    echo "Markdown report:"
    echo "  ${MD_REPORT}"
    echo
    echo "CSV report:"
    echo "  ${CSV_REPORT}"
    echo
    echo "UART logs:"
    echo "  ${UART_DIR}"
    echo
    echo "Original flash backup:"
    echo "  ${BACKUP_DIR}"
} | tee "${SUMMARY_REPORT}"

section "FINAL RESULT"

cat "${SUMMARY_REPORT}"

if (( FAIL_COUNT > 0 ||
      ERROR_COUNT > 0 ||
      RESTORE_FAILURES > 0 )); then
    echo
    echo "SUITE COMPLETED WITH FAILURES."
    exit 1
fi

echo
echo "SUITE COMPLETED WITHOUT AUTOMATIC FAILURES."
