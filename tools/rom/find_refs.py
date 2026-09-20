#!/usr/bin/env python3
"""Find raw pointer words and simple LUI/%LO references in a big-endian ROM."""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from romlib import ROM_TEXT_END, ROM_TEXT_START, containing_function, load_functions, load_rom, lookup, rom_to_vram, word_at


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address")
    parser.add_argument("--rom")
    args = parser.parse_args()
    functions = load_functions()
    try:
        address = int(args.address, 16)
    except ValueError:
        address = lookup(functions, args.address)[0]
    if address < 0x80000000:
        address |= 0x80000000
    rom = load_rom(args.rom)
    print(f"raw references to 0x{address:08X}")
    for offset in range(0, len(rom) - 3, 4):
        if word_at(rom, offset) == address:
            print(f"  ROM 0x{offset:X} (vram 0x{rom_to_vram(offset):08X})")
    end = ROM_TEXT_END or len(rom)
    hi = ((address + 0x8000) >> 16) & 0xFFFF
    lo = address & 0xFFFF
    for offset in range(ROM_TEXT_START, min(end, len(rom)), 4):
        word = word_at(rom, offset)
        if word >> 26 == 0x0F and (word & 0xFFFF) == hi:
            next_word = word_at(rom, offset + 4) if offset + 4 < len(rom) else 0
            if next_word & 0xFFFF == lo:
                site = rom_to_vram(offset)
                function = containing_function(functions, site)
                print(f"  LUI/LO at 0x{site:08X} in {function[2] if function else '?'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
