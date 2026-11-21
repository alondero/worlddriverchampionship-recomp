"""Report worker IP samples, with approximate symbol intervals without DWARF."""
import argparse
import bisect
from collections import Counter
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "boss"))
from profile_report import image_base, image_size, in_image, read_samples, resolve


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    nm, addr2line = shutil.which("nm"), shutil.which("addr2line")
    if not nm or not addr2line:
        raise SystemExit("MinGW nm and addr2line must be on PATH")
    base, size = image_base(args.exe), image_size(args.exe)
    samples, dropped = in_image(read_samples(args.samples), size)
    frames = resolve(addr2line, args.exe, [base + rva for rva in samples])
    result = subprocess.run([nm, "-n", "-C", "--defined-only", str(args.exe)],
                            capture_output=True, text=True, check=True)
    symbols = {}
    for line in result.stdout.splitlines():
        match = re.match(r"^([0-9a-fA-F]+) [tT] (.+)$", line)
        if match and not match[2].startswith("."):
            symbols[int(match[1], 16)] = match[2]
    addresses = sorted(symbols)
    counts = Counter()
    fallback = 0
    for rva, count in samples.items():
        address = base + rva
        chain = frames.get(address, [])
        function = chain[0][0] if chain else "??"
        if function == "??":
            index = bisect.bisect_right(addresses, address) - 1
            if index >= 0:
                function = "[symbol interval] " + symbols[addresses[index]]
                fallback += count
        counts[function] += count
    total = sum(samples.values())
    if not total:
        raise SystemExit("No in-image worker samples")
    print(args.samples.read_text().splitlines()[0])
    print(f"{total} in-image samples; {fallback} use approximate symbol intervals; {dropped} invalid offsets omitted")
    print("Percentages exclude DLL/other outside-image observations. No call stacks are recorded.")
    for function, count in counts.most_common(40):
        print(f"{100 * count / total:6.2f}% {count:6d} {function}")


if __name__ == "__main__":
    main()
