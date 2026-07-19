#!/usr/bin/env bash

secure-boot-hil run \
  --repo-root "${SECURE_BOOT_REPO_ROOT:-$PWD}" \
  --uart "${SECURE_BOOT_UART:-/dev/ttyUSB0}" \
  --baud "${SECURE_BOOT_BAUD:-115200}" \
  --capture-seconds "${SECURE_BOOT_CAPTURE_SECONDS:-5}" \
  --reset-cycles "${SECURE_BOOT_RESET_CYCLES:-10}" \
  "$@"
