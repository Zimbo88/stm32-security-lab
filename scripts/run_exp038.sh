#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Verwendung: $0 RUN_NAME" >&2
    exit 2
fi

name="$1"

case "$name" in
    *[!A-Za-z0-9_-]*|'')
        echo "Ungültiger Laufname: $name" >&2
        exit 2
        ;;
esac

directory="logs/exp038_access_consistency"
temporary="$directory/.${name}.log.tmp"
final="$directory/${name}.log"

rm -f "$temporary"

set +e
timeout 30s openocd \
    -c "set RUN_NAME $name" \
    -f "$directory/probe_access.cfg" \
    2>&1 | tee "$temporary"

status=${PIPESTATUS[0]}
set -e

if [[ $status -ne 0 ]]; then
    echo
    echo "ERROR: OpenOCD exited with status $status"
    echo "Temporäres Log: $temporary"
    exit "$status"
fi

grep -q "^EXP038_ACCESS_COMPLETE|${name}$" "$temporary" || {
    echo "ERROR: Completion marker is missing."
    exit 1
}

count="$(
    grep -Ec \
    '^[A-Z][A-Z0-9_]*\|0x[0-9A-Fa-f]+\|(OK|FAIL)(\|0x[0-9A-Fa-f]+)?$' \
    "$temporary"
)"

if [[ "$count" -ne 27 ]]; then
    echo "ERROR: Expected 27 records, found: $count"
    exit 1
fi

mv "$temporary" "$final"

echo
echo "OK: $final"
echo "Records: $count"
