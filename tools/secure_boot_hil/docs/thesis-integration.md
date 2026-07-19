# Thesis Integration

The HIL framework is intended to provide repeatable evidence for controlled
secure-boot experiments.

Use `results.json`, `results.csv`, `performance.json`, and UART evidence files
as the primary data sources. These files distinguish:

- firmware-reported cryptographic timing,
- host-side command and capture timing,
- flash programming and backup timing,
- total test duration.

Do not claim statistical significance from the generated summaries alone. The
framework reports descriptive statistics only.

For thesis appendices or portfolio review, include:

- repository commit from `environment.json`,
- selected layout profile from `configuration.json`,
- test catalog from `test-plan.json`,
- restore evidence from `restore-verification.json`,
- UART frames for each security-relevant result,
- exact mutation provenance from `mutation-manifest.json`.

Hardware-only validation that remains outside this framework includes electrical
characterization, option-byte protection policy, WRP/RDP provisioning, and
fault-injection campaigns.
