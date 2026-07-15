# Logic-analyzer captures

This directory contains raw sigrok captures collected during the STM32F429
security laboratory.

## EXP008 reset capture

The capture records digital signals associated with reset and board activity.

Capture format:
- sigrok session file
- Logic analyzer: FX2/Saleae-compatible device
- Sampling rate: 24 MHz
- Digital channels: D0 through D7

The raw capture is retained so that timing and signal behavior can be reviewed
later using PulseView or sigrok-cli.
