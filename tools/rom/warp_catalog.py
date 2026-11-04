"""Print WDC_WARP track and vehicle IDs from the verified USA ROM."""
import argparse
import hashlib
import json
import struct
import tomllib

from romlib import ANALYSIS_TOML, load_rom, vram_to_rom


def catalog(rom: bytes) -> dict:
    expected = tomllib.loads(ANALYSIS_TOML.read_text(encoding="utf-8"))["rom"]
    if len(rom) != expected["rom_size"] or hashlib.sha256(rom).hexdigest() != expected["sha256"]:
        raise ValueError("ROM identity does not match rom-analysis.toml")

    def word(address):
        return struct.unpack_from(">I", rom, vram_to_rom(address))[0]

    def string(address):
        offset = vram_to_rom(address)
        return rom[offset:rom.index(0, offset)].decode("ascii")

    tracks = []
    for index in range(58):
        address = 0x80096534 + index * 80
        city = string(word(0x80090950 + word(address) * 68))
        layout = "ABC"[word(address + 16)]
        reverse = "R" if rom[vram_to_rom(address + 12)] else ""
        tracks.append({"id": index + 1, "name": f"{city} {layout}{reverse}"})
    cars = [{"id": index, "name": string(word(0x80081030 + index * 128))} for index in range(35)]
    return {"tracks": tracks, "cars": cars}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        entries = catalog(load_rom(args.rom))
    except ValueError as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(entries, indent=2))
    else:
        for kind, rows in entries.items():
            print(kind + ":")
            for row in rows:
                print(f"  {row['id']:2}: {row['name']}")


if __name__ == "__main__":
    main()
