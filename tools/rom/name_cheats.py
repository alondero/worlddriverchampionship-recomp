"""Check and report the USA ROM's player-name cheat comparisons (no emulator needed)."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct

from romlib import ROM_TEXT_END, ROM_TEXT_START, load_rom, rom_to_vram, vram_to_rom


SHA256 = "e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97"


def inspect_names(rom: bytes) -> dict:
    """Decode names/ranges from instructions, rejecting any unreviewed ROM revision."""
    if hashlib.sha256(rom).hexdigest() != SHA256:
        raise ValueError("name cheat evidence requires the reviewed USA ROM SHA-256")

    def word(address):
        return struct.unpack_from(">I", rom, vram_to_rom(address))[0]

    def string(address):
        start = vram_to_rom(address)
        if not 0 <= start < len(rom):
            raise ValueError(f"string outside ROM: {address:#x}")
        return rom[start:rom.index(0, start)].decode("ascii")

    def require(address, expected):
        actual = word(address)
        if actual != expected:
            raise ValueError(f"unexpected instruction at {address:#x}: {actual:#x}")

    compare = 0x0C000000 | ((0x80019F34 >> 2) & 0x03FFFFFF)
    get_name = 0x0C000000 | ((0x80056D24 >> 2) & 0x03FFFFFF)
    sites = [rom_to_vram(offset)
             for offset in range(ROM_TEXT_START, ROM_TEXT_END, 4)
             if struct.unpack_from(">I", rom, offset)[0] == compare]
    # Every direct call to the original string comparator has a nearby player
    # name accessor. This is a bounded CPU-code search, not a claim about PAL.
    if len(sites) != 12:
        raise ValueError(f"expected 12 direct string comparisons, found {len(sites)}")
    output = []
    for site in sites:
        if get_name not in [word(a) for a in range(site - 80, site, 4)]:
            raise ValueError(f"comparison lacks nearby name accessor at {site:#x}")
        hi, lo = word(site - 8), word(site - 4)
        if hi >> 16 != 0x3C05 or lo >> 16 != 0x24A5:
            raise ValueError(f"unexpected string pointer construction at {site:#x}")
        low = lo & 0xFFFF
        address = ((hi & 0xFFFF) << 16) + (low - 0x10000 if low & 0x8000 else low)
        entry = {"name": string(address), "string": f"0x{address:08X}",
                 "comparison": f"0x{site:08X}"}
        if 0x80065928 <= site < 0x80065D30:
            start, end = word(site + 12), word(site + 44)
            if (start >> 16 != 0x2410 and start != 0x00008025) or end >> 16 != 0x2401:
                raise ValueError(f"unexpected track loop bounds at {site:#x}")
            require(site + 32, 0x0C015C03)  # jal func_8005700C
            require(site + 36, 0x24050001)  # a1 = available
            indices = list(range(0 if start == 0x00008025 else start & 0xFFFF,
                                 end & 0xFFFF))
            families = {word(0x80096534 + 80 * i) for i in indices}
            if len(families) != 1:
                raise ValueError(f"track range crosses families at {site:#x}")
            family = families.pop()
            entry.update(track_indices=indices, family_id=family,
                         venue=string(word(0x80090950 + 68 * family)))
        output.append(entry)

    # Pin the non-track consumers rather than guessing effects from the strings.
    require(0x80031720, 0x8C63DF80)  # native pink flag -> v1
    require(0x80031754, 0x2C430001)  # name matched -> v1
    require(0x800364EC, 0x2401001B)  # selected model = 27
    require(0x80036528, 0xAC2E2350)  # native EVILROB runtime flag
    require(0x8003652C, 0x24130023)  # alternate physics row = 35
    require(0x80016EC0, 0x8F392350)  # EVILROB flag -> t9
    require(0x80016EFC, 0x3C013FC0)  # conditional 1.5 multiplier
    changes = []
    for offset in range(0, 160, 4):
        values = [struct.unpack_from(">f", rom,
                  vram_to_rom(0x80090CD0 + 160 * row + offset))[0]
                  for row in (27, 35)]
        if values[0] != values[1]:
            changes.append({"offset": f"0x{offset:02X}",
                            "normal": values[0], "evilrob": values[1]})
    return {"sha256": SHA256, "names": output,
            "evilrob_car": string(word(0x80081030 + 128 * 27)),
            "evilrob_physics_changes": changes,
            "scope": "all 12 direct calls to 0x80019F34 in measured USA CPU code"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom")
    args = parser.parse_args()
    try:
        report = inspect_names(load_rom(args.rom))
    except ValueError as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
