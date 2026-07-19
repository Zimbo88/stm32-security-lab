#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Verwendung: $0 SNAPSHOT_NAME HALT_ONLY|RESET_HALT" >&2
    exit 2
fi

name="$1"
mode="$2"

case "$name" in
    *[!A-Za-z0-9_-]*|'')
        echo "Ungültiger Snapshot-Name: $name" >&2
        exit 2
        ;;
esac

case "$mode" in
    HALT_ONLY|RESET_HALT)
        ;;
    *)
        echo "Ungültiger Modus: $mode" >&2
        exit 2
        ;;
esac

directory="logs/exp037_reset_state_comparison"
final_log="$directory/${name}.log"
temporary_log="$directory/.${name}.log.tmp"

rm -f "$temporary_log"

set +e
timeout 30s openocd \
    -c "set SNAPSHOT_NAME $name" \
    -c "set RESET_MODE $mode" \
    -f "$directory/capture_snapshot.cfg" \
    2>&1 | tee "$temporary_log"

openocd_status=${PIPESTATUS[0]}
set -e

if [[ $openocd_status -ne 0 ]]; then
    echo
    echo "ERROR: OpenOCD exited with status $openocd_status"
    echo "Temporäres Log bleibt erhalten: $temporary_log"
    exit "$openocd_status"
fi

if ! grep -q "^EXP037_SNAPSHOT_COMPLETE|${name}$" "$temporary_log"; then
    echo
    echo "ERROR: Completion marker is missing."
    echo "The previous valid log was not overwritten."
    exit 1
fi

record_count="$(
    grep -Ec \
    '^[A-Z][A-Z0-9_]*\|(0x[0-9A-Fa-f]+|REG:[a-z0-9_]+)\|(OK|FAIL)(\|0x[0-9A-Fa-f]+)?$' \
    "$temporary_log"
)"

if [[ "$record_count" -ne 33 ]]; then
    echo
    echo "ERROR: Expected 33 records, found: $record_count"
    exit 1
fi

mv "$temporary_log" "$final_log"

echo
echo "OK: Snapshot gespeichert: $final_log"
echo "Records: $record_count"
