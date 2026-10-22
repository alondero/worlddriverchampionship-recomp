"""Summarise adjacent-triangle seams in a WDC_RDP_CAPTURE stream.

Usage: python -B tools/rdp/analyze_seams.py CAPTURE.rdp [--scale 4.8] [--worst 5]

Compares the pinned RT64 three-point reconstruction ("rt64") with geometry
bounded by the RDP's own H/M/L edges ("rdp"). Reports uncovered GPU sample
centres between neighbours at the given resolution scale.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rdp_triangles import gap_samples, read_capture, shared_edges, triangles  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", type=Path)
    parser.add_argument("--scale", type=float, action="append")
    parser.add_argument("--worst", type=int, default=5)
    args = parser.parse_args()
    tris = list(triangles(read_capture(args.capture.read_bytes())))
    pairs = list(shared_edges(tris))
    print(f"triangles={len(tris)} shared_edges={len(pairs)}")
    flags = sum(1 for t in tris if (t.interior_side() > 0) != t.left_major)
    print(f"left-major flag disagrees with measured span side: {flags}")
    for name, column in (("top |H-M|@YH", 0), ("middle |M-XL|@YM", 1), ("bottom |H-L|@YL", 2)):
        values = sorted(t.closure()[column] for t in tris)
        if values:
            pick = lambda q: values[min(len(values) - 1, int(q * len(values)))]
            print(f"closure {name}: median={pick(.5):.4f} p90={pick(.9):.4f} p99={pick(.99):.4f} max={values[-1]:.4f}")
    for scale in args.scale or [1.0, 4.8]:
        for model in ("rt64", "rdp"):
            results = [(gap_samples(p, model, scale), p) for p in pairs]
            holes = sum(r[0][0] for r in results)
            seams = sum(1 for r in results if r[0][0])
            widest = max((r[0][1] for r in results), default=0.0)
            print(f"scale={scale:g} model={model}: gap_samples={holes} seams={seams} widest_gap_px={widest:.4f}")
            if model == "rt64":
                for (count, width), p in sorted(results, key=lambda r: -r[0][0])[:args.worst]:
                    if count:
                        print(f"  {count} samples, {width:.4f}px: cmd {p.left.index}/{p.left_edge}"
                              f" | cmd {p.right.index}/{p.right_edge}")


if __name__ == "__main__":
    main()
