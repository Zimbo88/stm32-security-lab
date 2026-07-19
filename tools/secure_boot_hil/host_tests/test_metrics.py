from __future__ import annotations

from secure_boot_hil.metrics import aggregate_metric, parse_boot_metrics


def test_boot_metric_parsing() -> None:
    metrics = parse_boot_metrics(
        """
CPU clock        = 168000000 Hz
SHA-512          = 42 us
Ed25519          = 120 us
Verification     = 180 us
"""
    )
    assert metrics == {
        "cpu_clock_hz": 168000000,
        "sha512_us": 42,
        "ed25519_us": 120,
        "verification_us": 180,
    }


def test_metric_aggregation() -> None:
    result = aggregate_metric([3, 1, 2, 4])
    assert result["sample_count"] == 4
    assert result["minimum"] == 1.0
    assert result["maximum"] == 4.0
    assert result["median"] == 2.5
    assert "p25" in result
