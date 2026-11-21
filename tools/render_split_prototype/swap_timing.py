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


def group_intervals(bindings, swaps):
    """Compare game-side screen submission and RT64 swap spacing across group edges."""
    if [b["id"] for b in bindings] != [s["id"] for s in swaps]:
        raise ValueError("Group intervals require ordered, matching swap IDs")
    groups = {"within_group": [], "between_groups": []}
    for i in range(1, len(bindings)):
        first, second = bindings[i-1:i+1]
        group = "within_group" if first["frame"] == second["frame"] else "between_groups"
        groups[group].append(((second["screen_us"] - first["screen_us"]) / 1000,
                              (swaps[i]["us"] - swaps[i-1]["us"]) / 1000))
    result = {}
    for group, rows in groups.items():
        record = {"interval_count": len(rows)}
        if rows:
            for name, values in zip(("screen", "swap"), zip(*rows)):
                ordered = sorted(values)
                record[name + "_p50_ms"] = ordered[math.ceil(.50 * len(rows)) - 1]
                record[name + "_p95_ms"] = ordered[math.ceil(.95 * len(rows)) - 1]
        result[group] = record
    return result
