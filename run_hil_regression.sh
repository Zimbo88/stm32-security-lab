#!/usr/bin/env bash
set -euo pipefail

RUNS=5

for i in $(seq 1 $RUNS); do
    echo
    echo "==============================="
    echo "HIL RUN $i / $RUNS"
    echo "==============================="

    secure-boot-hil run \
      --repo-root ~/stm32-security-lab \
      --uart /dev/ttyUSB0 \
      --baud 115200 \
      --capture-seconds 5 \
      --reset-cycles 10 \
      --non-interactive

done

echo
echo "Alle Regression-Läufe abgeschlossen."
