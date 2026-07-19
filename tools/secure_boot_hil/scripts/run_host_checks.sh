#!/usr/bin/env bash

status=0

cd "$(dirname "$0")/.." || exit 1

PYTHONDONTWRITEBYTECODE=1 python3 -m pytest -q -p no:cacheprovider host_tests || status=1
python3 -m ruff check . || status=1
python3 -m mypy secure_boot_hil || status=1

exit "$status"
