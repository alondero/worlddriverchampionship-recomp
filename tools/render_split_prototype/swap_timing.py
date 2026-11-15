"""Diagnostic interval distribution for already-validated actual swap events."""
import math


def summarize_swaps(swaps, hz):
    if not math.isfinite(hz) or hz <= 0 or len(swaps) < 2:
        raise ValueError("Swap timing requires a positive rate and at least two events")
    intervals = [(b["us"] - a["us"]) / 1000 for a, b in zip(swaps, swaps[1:])]
    if any(not math.isfinite(value) or value <= 0 for value in intervals):
        raise ValueError("Swap timestamps must increase")
    ordered = sorted(intervals)
    period = 1000 / hz
    def percentile(fraction):
        # Nearest-rank convention, including small bounded captures.
        return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]
    duration = sum(intervals) / 1000
    return {"target_hz": hz, "target_period_ms": period,
            "swap_count": len(swaps), "interval_count": len(intervals), "duration_s": duration,
            "rate_hz": len(intervals) / duration,
            "min_ms": ordered[0], "p50_ms": percentile(.50), "p95_ms": percentile(.95),
            "p99_ms": percentile(.99), "max_ms": ordered[-1],
            "short_intervals": sum(value < period * .75 for value in intervals),
            "long_intervals": sum(value > period * 1.5 for value in intervals)}
