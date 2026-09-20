#!/usr/bin/env python3
"""List direct JAL callers of a generated function."""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from romlib import (ROM_TEXT_END, ROM_TEXT_START, containing_function, load_functions,
                    load_rom, lookup, rom_to_vram, word_at)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target")
    parser.add_argument("--rom")
    args = parser.parse_args()
    rom = load_rom(args.rom)
    functions = load_functions()
    vram, _, name = lookup(functions, args.target)
    wanted = (vram >> 2) & 0x03FFFFFF
    end = ROM_TEXT_END or len(rom)
    for offset in range(ROM_TEXT_START, min(end, len(rom)), 4):
        word = word_at(rom, offset)
        if word >> 26 == 0x03 and (word & 0x03FFFFFF) == wanted:
            site = rom_to_vram(offset)
            caller = containing_function(functions, site)
            print(f"0x{site:08X} in {caller[2] if caller else '?'}")
    print(f"{name} (0x{vram:08X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
