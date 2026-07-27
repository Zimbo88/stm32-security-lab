#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_ROOT="${ROOT}/hil-results"
PORT=""
PACKAGE=""
PUBLIC_KEY_HEADER="${ROOT}/firmware/exp045_bootloader_v2/src/firmware_public_key.h"
BLOCK_SIZE="512"
TIMEOUT="15.0"
RUN_UPDATE=0
ALLOW_TARGET_WRITE=0
RUN_RESET=0
SKIP_BUILD=0

usage() {
    cat <<'EOF'
Usage:
  tools/run_secure_update_hardware_test.sh [options]

Safe by default:
  - builds firmware,
  - verifies a package locally when --package is supplied,
  - runs read-only stm32ctl info/status when --port is supplied,
  - writes timestamped logs under hil-results/.

Options:
  --port PORT                 UART port, for example /dev/ttyUSB0.
  --package PATH              Signed update package to verify or send.
  --public-key-header PATH    Public key header for local package verification.
  --block-size BYTES          stm32ctl update block size, default 512.
  --timeout SECONDS           stm32ctl timeout, default 15.0.
  --run-update                Run stm32ctl update. Requires --allow-target-write.
  --allow-target-write        Explicitly allow UART firmware update writes.
  --reset-target              Request stm32ctl reset after read-only checks.
  --skip-build                Skip local clean builds.
  -h, --help                  Show this help.

This script never changes RDP, never changes Option Bytes, and never invokes
st-flash. Bootloader flashing and metadata provisioning remain manual steps.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            PORT="$2"
            shift 2
            ;;
        --package)
            PACKAGE="$2"
            shift 2
            ;;
        --public-key-header)
            PUBLIC_KEY_HEADER="$2"
            shift 2
            ;;
        --block-size)
            BLOCK_SIZE="$2"
            shift 2
            ;;
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        --run-update)
            RUN_UPDATE=1
            shift
            ;;
        --allow-target-write)
            ALLOW_TARGET_WRITE=1
            shift
            ;;
        --reset-target)
            RUN_RESET=1
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "${RUN_UPDATE}" -eq 1 && "${ALLOW_TARGET_WRITE}" -ne 1 ]]; then
    echo "--run-update requires --allow-target-write" >&2
    exit 2
fi

if [[ "${RUN_UPDATE}" -eq 1 && -z "${PACKAGE}" ]]; then
    echo "--run-update requires --package" >&2
    exit 2
fi

if [[ "${RUN_UPDATE}" -eq 1 && -z "${PORT}" ]]; then
    echo "--run-update requires --port" >&2
    exit 2
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
LOG_DIR="${LOG_ROOT}/secure-update-${STAMP}"
mkdir -p "${LOG_DIR}"

SUMMARY="${LOG_DIR}/summary.txt"
touch "${SUMMARY}"

log_summary() {
    printf '%s\n' "$*" | tee -a "${SUMMARY}"
}

run_logged() {
    local name="$1"
    shift
    log_summary "RUN ${name}: $*"
    if "$@" >"${LOG_DIR}/${name}.log" 2>&1; then
        log_summary "PASS ${name}"
    else
        local status=$?
        log_summary "FAIL ${name}: exit ${status}"
        return "${status}"
    fi
}

log_summary "secure-update HIL helper log: ${LOG_DIR}"
log_summary "RDP/Option-Byte safety: this script does not modify RDP or Option Bytes."

if [[ "${SKIP_BUILD}" -ne 1 ]]; then
    run_logged bootloader_build \
        make -C "${ROOT}/firmware/exp045_bootloader_v2" clean report
    run_logged exp066_slot_a_build \
        make -C "${ROOT}/firmware/exp066_research_platform_core" clean all LAYOUT_PROFILE=stm32f429_1m
    run_logged exp066_slot_b_build \
        make -C "${ROOT}/firmware/exp066_research_platform_core" SLOT=b BUILD=build/slot_b PROJECT=exp066_research_platform_core_slot_b clean all LAYOUT_PROFILE=stm32f429_1m
fi

if [[ -n "${PACKAGE}" ]]; then
    run_logged package_verify \
        env PACKAGE="${PACKAGE}" \
            PUBLIC_KEY_HEADER="${PUBLIC_KEY_HEADER}" \
            PYTHONPATH="${ROOT}/tools" \
            python3 - <<'PY'
import os
from pathlib import Path
from stm32ctl.package import load_and_verify_package

package = load_and_verify_package(
    Path(os.environ["PACKAGE"]),
    public_key_header=Path(os.environ["PUBLIC_KEY_HEADER"]),
)
print(f"path={package.path}")
print(f"slot={package.slot}")
print(f"image_version={package.image_version}")
print(f"payload_size={package.payload_size}")
print(f"package_size={package.package_size}")
print(f"sha512={package.payload_sha512}")
PY
fi

if [[ -n "${PORT}" ]]; then
    run_logged stm32ctl_info \
        env PYTHONPATH="${ROOT}/tools" python3 -m stm32ctl \
            --port "${PORT}" --timeout "${TIMEOUT}" info
    run_logged stm32ctl_status \
        env PYTHONPATH="${ROOT}/tools" python3 -m stm32ctl \
            --port "${PORT}" --timeout "${TIMEOUT}" status
fi

if [[ "${RUN_UPDATE}" -eq 1 ]]; then
    run_logged stm32ctl_update \
        env PYTHONPATH="${ROOT}/tools" python3 -m stm32ctl \
            --port "${PORT}" \
            --timeout "${TIMEOUT}" \
            update \
            --package "${PACKAGE}" \
            --public-key-header "${PUBLIC_KEY_HEADER}" \
            --block-size "${BLOCK_SIZE}"
fi

if [[ "${RUN_RESET}" -eq 1 ]]; then
    if [[ -z "${PORT}" ]]; then
        echo "--reset-target requires --port" >&2
        exit 2
    fi
    run_logged stm32ctl_reset \
        env PYTHONPATH="${ROOT}/tools" python3 -m stm32ctl \
            --port "${PORT}" --timeout "${TIMEOUT}" reset
fi

log_summary "Logs written to ${LOG_DIR}"
