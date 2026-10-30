#!/usr/bin/env python3
"""Attribute wdc_boss_bench --profile samples to BOSS RSP labels.

Each sample is an instruction-pointer offset into wdc_boss_bench.exe.
addr2line (MinGW binutils) resolves it, with inlined frames, to source lines.
The outermost frame in the generated src/aspMain.cpp gives the RSP
instruction label (`L_xxxx:` before that line); the innermost frame names the
helper doing the work, such as an RSP vector-unit operation. Optimised code
makes line attribution approximate; read clusters, not single labels.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path


def image_base(exe: Path) -> int:
    data = exe.read_bytes()[:4096]
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 24)[0] != 0x20B:
        raise SystemExit(f"{exe}: not a PE32+ executable")
    return struct.unpack_from("<Q", data, pe + 24 + 24)[0]


def read_samples(path: Path) -> dict[int, int]:
    samples: dict[int, int] = {}
    for line in path.read_text().splitlines():
        if line and not line.startswith("#"):
            fields = line.split()
            if len(fields) != 2:
                raise SystemExit(f"{path}: bad sample line: {line}")
            rva, count = fields
            samples[int(rva, 16)] = int(count)
    return samples


def label_index(asp: Path) -> tuple[list[int], list[int]]:
    lines, labels = [], []
    for number, text in enumerate(asp.read_text().splitlines(), start=1):
        match = re.match(r"L_([0-9A-Fa-f]+):", text)
        if match:
            lines.append(number)
            labels.append(int(match.group(1), 16))
    return lines, labels


def parse_frames(output: str) -> dict[int, list[tuple[str, str]]]:
    """Read addr2line output into frames per address, innermost first."""
    frames: dict[int, list[tuple[str, str]]] = {}
    current = None
    lines = output.splitlines()
    i = 0
    while i < len(lines):
        if lines[i].startswith("0x"):
            current = int(lines[i], 16)
            frames[current] = []
            i += 1
        elif current is not None and i + 1 < len(lines):
            frames[current].append((lines[i], lines[i + 1]))
            i += 2
        else:
            i += 1
    return frames


def resolve(addr2line: str, exe: Path, addresses: list[int]) -> dict[int, list[tuple[str, str]]]:
    """Map each address to its frames, innermost first: (function, file:line)."""
    result = subprocess.run(
        [addr2line, "-a", "-f", "-i", "-C", "-e", str(exe)],
        input="\n".join(f"{a:#x}" for a in addresses) + "\n",
        capture_output=True, text=True, check=True)
    return parse_frames(result.stdout)


def attribute(samples: dict[int, int], frames: dict[int, list[tuple[str, str]]],
              base: int, label_lines: list[int], labels: list[int],
              window: int) -> tuple[collections.Counter[str], collections.Counter[str],
                                    collections.Counter[str]]:
    """Group the sample counts by RSP label, IMEM window and innermost function."""
    by_label: collections.Counter[str] = collections.Counter()
    by_window: collections.Counter[str] = collections.Counter()
    by_leaf: collections.Counter[str] = collections.Counter()
    for rva, count in samples.items():
        chain = frames.get(base + rva, [])
        leaf = chain[0][0] if chain else "??"
        by_leaf[re.sub(r"\(.*", "", leaf)] += count
        # Locations look like "F:/.../src/aspMain.cpp:1234 (discriminator 2)".
        asp_lines = [int(m.group(2)) for _, loc in chain
                     if (m := re.match(r"(.*):(\d+)", loc)) and Path(m.group(1)).name == "aspMain.cpp"]
        line = asp_lines[-1] if asp_lines else 0
        slot = bisect.bisect_right(label_lines, line) - 1 if line else -1
        if slot < 0:
            outer = re.sub(r"\(.*", "", chain[-1][0]) if chain else "??"
            by_label[f"(outside bossMain: {outer})"] += count
            by_window[f"(outside bossMain: {outer})"] += count
            continue
        pc = labels[slot]
        by_label[f"L_{pc:04X}"] += count
        start = pc - pc % window
        by_window[f"IMEM {start:04X}-{start + window - 1:04X}"] += count
    return by_label, by_window, by_leaf


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("samples", type=Path)
    parser.add_argument("--exe", type=Path, default=Path("build/wdc_boss_bench.exe"))
    parser.add_argument("--asp", type=Path, default=Path("src/aspMain.cpp"))
    parser.add_argument("--window", type=lambda s: int(s, 0), default=0x40,
                        help="IMEM bytes per grouped window (default 0x40)")
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()
    addr2line = shutil.which("addr2line")
    if not addr2line:
        raise SystemExit("addr2line not found on PATH (MinGW binutils)")

    samples = read_samples(args.samples)
    total = sum(samples.values())
    if not total:
        raise SystemExit("no samples")
    base = image_base(args.exe)
    frames = resolve(addr2line, args.exe, [base + rva for rva in samples])
    label_lines, labels = label_index(args.asp)
    by_label, by_window, by_leaf = attribute(
        samples, frames, base, label_lines, labels, args.window)

    def table(title: str, counter: collections.Counter[str]) -> None:
        print(f"\n{title}")
        for name, count in counter.most_common(args.top):
            print(f"  {100.0 * count / total:5.1f}%  {count:7d}  {name}")

    print(f"{total} samples from {args.samples}")
    table(f"By IMEM window ({args.window:#x} bytes)", by_window)
    table("By RSP label", by_label)
    table("By innermost function", by_leaf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
