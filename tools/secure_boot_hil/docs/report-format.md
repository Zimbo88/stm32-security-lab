# Report Format

Every run directory is self-contained.

Required files:

- `run.log`: JSON-lines event log.
- `environment.json`: Git state, tool versions, and configured UART.
- `configuration.json`: resolved HIL configuration and region map.
- `test-plan.json`: stable test definitions selected for the run.
- `image-manifest.json`: cached valid images and derived fault images.
- `mutation-manifest.json`: deterministic mutation provenance.
- `results.json`: complete structured result data.
- `results.csv`: compact table for analysis.
- `report.md`: human-readable Markdown report.
- `report.html`: self-contained HTML report without JavaScript.
- `junit.xml`: CI-ingestible JUnit report.
- `performance.json`: parsed boot metrics and aggregate statistics.
- `backup-manifest.json`: original flash backups.
- `restore-verification.json`: post-restore readback comparison.
- `uart/raw/`: raw UART byte streams.
- `uart/frames/`: extracted boot frames.
- `flash-backup/`: original region dumps.
- `restore-readback/`: post-restore region dumps.
- `image-cache/`: immutable run-specific build and mutation artifacts.

Report timestamps are UTC ISO-8601 strings. JSON output uses deterministic key
ordering where practical.

Heuristic classifications are not used for pass/fail decisions. Security
decisions remain inside the firmware verifier and boot policy.
