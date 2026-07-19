"""Boot metric extraction and aggregation."""

from __future__ import annotations

import re
import statistics
from collections.abc import Iterable

METRIC_PATTERNS = {
    "cpu_clock_hz": re.compile(r"CPU clock\s*=\s*(\d+)\s*Hz"),
    "sha512_us": re.compile(r"SHA-512\s*=\s*(\d+)\s*us"),
    "ed25519_us": re.compile(r"Ed25519\s*=\s*(\d+)\s*us"),
    "verification_us": re.compile(r"Verification\s*=\s*(\d+)\s*us"),
}


def parse_boot_metrics(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, pattern in METRIC_PATTERNS.items():
        match = pattern.search(text)
        if match:
            values[name] = int(match.group(1))
    return values


def percentile(sorted_values: list[float], percentile_value: float) -> float:
    if not sorted_values:
        raise ValueError("percentile requires at least one sample")
    if len(sorted_values) == 1:
        return sorted_values[0]
    rank = (len(sorted_values) - 1) * percentile_value
    lower = int(rank)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = rank - lower
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * fraction


def aggregate_metric(samples: Iterable[float]) -> dict[str, float | int]:
    values = sorted(float(sample) for sample in samples)
    if not values:
        return {"sample_count": 0}
    result: dict[str, float | int] = {
        "sample_count": len(values),
        "minimum": min(values),
        "maximum": max(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "population_stddev": statistics.pstdev(values) if len(values) > 1 else 0.0,
    }
    if len(values) >= 4:
        result["p25"] = percentile(values, 0.25)
        result["p75"] = percentile(values, 0.75)
    if len(values) >= 20:
        result["p95"] = percentile(values, 0.95)
    return result


def aggregate_boot_metrics(
    metric_sets: Iterable[dict[str, float | int]],
) -> dict[str, dict[str, float | int]]:
    grouped: dict[str, list[float]] = {}
    for metric_set in metric_sets:
        for name, value in metric_set.items():
            grouped.setdefault(name, []).append(float(value))
    return {name: aggregate_metric(values) for name, values in sorted(grouped.items())}
