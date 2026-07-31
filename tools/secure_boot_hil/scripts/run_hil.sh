#!/usr/bin/env bash

UART_DEVICE="${SECURE_BOOT_UART:?Set SECURE_BOOT_UART to the local UART device before running HIL}"

secure-boot-hil run \
  --repo-root "${SECURE_BOOT_REPO_ROOT:-$PWD}" \
  --uart "${UART_DEVICE}" \
  --baud "${SECURE_BOOT_BAUD:-115200}" \
  --capture-seconds "${SECURE_BOOT_CAPTURE_SECONDS:-5}" \
  --reset-cycles "${SECURE_BOOT_RESET_CYCLES:-10}" \
  "$@"
