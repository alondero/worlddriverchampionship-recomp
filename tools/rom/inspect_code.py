"""Disassemble measured ROM ranges (requires the optional rabbitizer package)."""
from __future__ import annotations

import argparse
import struct

from romlib import load_rom, rom_to_vram, vram_to_rom


def main() -> None:
    import rabbitizer

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", type=lambda s: int(s, 0))
    parser.add_argument("--size", type=lambda s: int(s, 0), default=0x100)
    parser.add_argument("--rom")
    args = parser.parse_args()
    rom = load_rom(args.rom)
    start = vram_to_rom(args.address) if args.address >= 0x80000000 else args.address
    if start < 0 or start % 4 or args.size < 0 or args.size % 4 or start + args.size > len(rom):
        parser.error("range must be word-aligned and inside the ROM")
    for offset in range(start, start + args.size, 4):
        word = struct.unpack_from(">I", rom, offset)[0]
        address = rom_to_vram(offset)
        print(f"{offset:08X} {address:08X} {word:08X}  "
              f"{rabbitizer.Instruction(word, address).disassemble()}")


if __name__ == "__main__":
    main()
