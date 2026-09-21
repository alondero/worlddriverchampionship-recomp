"""Find SDK symbol candidates by comparing relocation-masked peer ROM bytes.

This prints evidence candidates, never edits the reviewed symbol map. Masks
ignore jump destinations and address immediates, so matches need inspection.
"""
from __future__ import annotations

import argparse
import struct
import tomllib
from pathlib import Path


def mask(word: int) -> int:
    op = word >> 26
    if op in (2, 3):
        return word & 0xFC000000
    if op == 15 or (op in (9, 13, 32, 33, 35, 36, 37, 40, 41, 43, 49, 53, 57, 61)
                    and (word >> 21) & 31 != 29):
        return word & 0xFFFF0000
    return word


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("peer_rom", type=Path)
    parser.add_argument("peer_symbols", type=Path)
    parser.add_argument("--rom", type=Path, default=Path("World Driver Championship (USA).z64"))
    args = parser.parse_args()
    peer = args.peer_rom.read_bytes()
    target = args.rom.read_bytes()
    words = struct.unpack(f">{len(target) // 4}I", target)
    masked = b"".join(struct.pack(">I", mask(w)) for w in words[:0x7D2B0 // 4])
    sections = tomllib.loads(args.peer_symbols.read_text(encoding="utf-8"))["section"]
    for section in sections:
        for f in section.get("functions", []):
            name, address, size = f["name"], f["vram"], f["size"]
            if name.startswith(("func_", "recomp_", "static_")) or size < 12:
                continue
            offset = section["rom"] + address - section["vram"]
            # Stop after the first return (includes delay); cap long functions
            # so a differing derived boundary does not prevent identification.
            length = min(size, 0x100)
            source = list(struct.unpack(f">{length // 4}I", peer[offset:offset + length]))
            if 0x03E00008 in source:
                source = source[:source.index(0x03E00008) + 2]
            pattern = b"".join(struct.pack(">I", mask(w)) for w in source)
            hits = []
            pos = masked.find(pattern, 0x1000)
            while pos >= 0:
                if pos % 4 == 0:
                    hits.append(hex(pos + 0x7FFFF400))
                pos = masked.find(pattern, pos + 1)
            print(f"{name:35} {len(source):3} words  {', '.join(hits) or 'no match'}")


if __name__ == "__main__":
    main()
