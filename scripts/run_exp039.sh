#!/usr/bin/env bash
set -euo pipefail

directory="logs/exp039_coresight_component_ids"
temporary="$directory/.probe_component_ids.log.tmp"
final="$directory/probe_component_ids.log"

rm -f "$temporary"

set +e
timeout 30s openocd \
    -f "$directory/probe_component_ids.cfg" \
    2>&1 | tee "$temporary"

status=${PIPESTATUS[0]}
set -e

if [[ $status -ne 0 ]]; then
    echo
    echo "FEHLER: OpenOCD-Status $status"
    echo "Temporäres Log: $temporary"
    exit "$status"
fi

grep -q '^EXP039_COMPONENT_IDS_COMPLETE$' "$temporary" || {
    echo "FEHLER: Abschlussmarkierung fehlt."
    exit 1
}

count="$(
    grep -Ec \
    '^[A-Z][A-Z0-9_]*\|(0x[0-9A-Fa-f]+|[0-9]+)\|(OK|FAIL)(\|0x[0-9A-Fa-f]+)?$' \
    "$temporary"
)"

if [[ "$count" -ne 100 ]]; then
    echo "FEHLER: Erwartet 100 Datensätze, gefunden: $count"
    exit 1
fi

mv "$temporary" "$final"

echo
echo "OK: $final"
echo "Datensätze: $count"
