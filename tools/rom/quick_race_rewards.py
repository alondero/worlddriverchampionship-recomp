"""Decode the verified USA ROM's Quick Race car/lap-count reward table."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import tomllib

from romlib import ANALYSIS_TOML, load_rom, vram_to_rom

REWARDS = 0x800962BC
CAR_NAMES = 0x80081030
TRACKS = 0x80096534
CITIES = 0x80090950


def reward_cells(table: bytes) -> list[tuple[int, ...]]:
    """Discard AI rank/profile columns, preserving signed rewards and row order."""
    if len(table) != 35 * 18:
        raise ValueError("expected 35 complete 18-byte reward records")
    return [struct.unpack_from(">9h", table, model * 18)[2::2] for model in range(35)]


def decode_rewards(rom: bytes) -> list[dict]:
    """Read immutable big-endian ROM data; never touch a live save or RDRAM."""
    expected = tomllib.loads(ANALYSIS_TOML.read_text(encoding="utf-8"))["rom"]
    if len(rom) != expected["rom_size"] or hashlib.sha256(rom).hexdigest() != expected["sha256"]:
        raise ValueError("ROM identity does not match rom-analysis.toml")

    def word(address: int) -> int:
        return struct.unpack_from(">I", rom, vram_to_rom(address))[0]

    # Recheck the consumer alongside the data: player position zero gates the
    # reward, and lap count selects four columns from the selected model row.
    instructions = {
        0x80066320: 0x8F39E130, 0x8006632C: 0x57200040,
        0x80066334: 0x8C620080, 0x80066368: 0x8C680008,
        0x80066384: 0x861062C0, 0x800663A4: 0x861062C4,
        0x800663C4: 0x861062C8, 0x800663E0: 0x861062CC,
        0x8004C974: 0x0C0198BB,
    }
    for address, expected_word in instructions.items():
        if word(address) != expected_word:
            raise ValueError(f"unexpected reward instruction at {address:#x}")

    def string(address: int) -> str:
        offset = vram_to_rom(address)
        return rom[offset:rom.index(0, offset)].decode("ascii")

    def car(model: int) -> str:
        return string(word(CAR_NAMES + model * 128))

    def track(index: int) -> str:
        address = TRACKS + index * 80
        city = string(word(CITIES + word(address) * 68))
        layout = "ABC"[word(address + 16)]
        reverse = "R" if rom[vram_to_rom(address + 12)] else ""
        return f"{city} {layout}{reverse}"

    rows = []
    start = vram_to_rom(REWARDS)
    cells = reward_cells(rom[start:start + 35 * 18])
    for model, values in enumerate(cells):
        # Each 18-byte row has an AI rank followed by four (AI profile,
        # signed reward) pairs. The profile is NOT the required race course.
        for index, laps in enumerate(range(2, 6)):
            reward = values[index]
            if not reward:
                continue
            kind = "track" if reward > 0 else "car"
            identity = abs(reward)
            rows.append({
                "model": model, "car": car(model), "laps": laps,
                "kind": kind, "id": identity,
                "reward": track(identity) if reward > 0 else car(identity),
                "address": f"0x{REWARDS + model * 18 + 4 + index * 4:08X}",
            })
    track_rows = [row for row in rows if row["kind"] == "track"]
    if (len(rows) != 76 or len(track_rows) != 50 or
            len({row["id"] for row in track_rows}) != 48):
        raise ValueError("unexpected Quick Race reward coverage")
    known = {(6, 2): 17, (3, 3): 41, (2, 2): 51, (0, 3): 2, (9, 4): 49}
    actual = {(row["model"], row["laps"]): row["id"] for row in track_rows}
    if any(actual.get(key) != reward for key, reward in known.items()):
        raise ValueError("published five track awards do not match the ROM")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom")
    parser.add_argument("--json", action="store_true", help="emit structured evidence")
    args = parser.parse_args()
    rom = load_rom(args.rom)
    try:
        rows = decode_rewards(rom)
    except ValueError as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(rows, indent=2))
        return
    print("| Car | Laps | Reward | Reward halfword |")
    print("| --- | --- | --- | --- |")
    for row in rows:
        print(f"| {row['car']} | {row['laps']} | {row['kind']}: {row['reward']} | `{row['address']}` |")


if __name__ == "__main__":
    main()
