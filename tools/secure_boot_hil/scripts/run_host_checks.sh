#!/usr/bin/env bash

status=0

cd "$(dirname "$0")/.." || exit 1

PYTHON="${PYTHON:-python3}"

PYTHONDONTWRITEBYTECODE=1 "$PYTHON" -m pytest -q -p no:cacheprovider host_tests || status=1
"$PYTHON" -m ruff check . || status=1
"$PYTHON" -m mypy secure_boot_hil || status=1

exit "$status"
