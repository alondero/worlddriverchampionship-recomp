"""Decode the verified USA ROM's complete button-cheat dispatch table."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct

from romlib import load_rom, vram_to_rom


SHA256 = "e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97"
TABLE = 0x8007DC48
BUTTONS = {
    1: "Right", 2: "Down", 4: "Left", 8: "Up", 0x10: "A", 0x20: "B",
    0x100: "C-Up", 0x200: "C-Right", 0x400: "C-Down", 0x800: "C-Left",
    0x1000: "L", 0x2000: "R", 0x4000: "Start", 0x8000: "Z",
}


def decode(rom: bytes) -> list[dict]:
    """Return ROM facts only; effects require tracing each handler separately."""
    if hashlib.sha256(rom).hexdigest() != SHA256:
        raise ValueError("button table addresses require the verified USA ROM")
    entries = []
    # The table is terminated by a pair of null pointers, not a guessed count.
    for index in range(32):
        handler, sequence = struct.unpack_from(">II", rom, vram_to_rom(TABLE + 8 * index))
        if handler == sequence == 0:
            return entries
        if not (0x80000400 <= handler < 0x8007C6B0 and
                0x8007D870 <= sequence < TABLE):
            raise ValueError("invalid button table pointer")
        history = []
        for offset in range(33):
            action = struct.unpack_from(">H", rom, vram_to_rom(sequence) + 2 * offset)[0]
            if action == 0xFFFF:
                break
            history.append(action)
        else:
            raise ValueError("button sequence exceeds the 32-halfword history")
        if not history or any(action not in BUTTONS and action != 0 for action in history):
            raise ValueError("invalid button action")
        # Release transitions are explicit zero actions in newest-first history.
        if any(history[i] != 0 for i in range(1, len(history), 2)):
            raise ValueError("unexpected button release encoding")
        entries.append({
            "handler": f"0x{handler:08X}",
            "sequence": f"0x{sequence:08X}",
            "presses": [BUTTONS[action] for action in reversed(history) if action],
        })
    raise ValueError("unterminated button table")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        entries = decode(load_rom(args.rom))
    except ValueError as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps({"sha256": SHA256, "entries": entries}, indent=2))
    else:
        print(f"USA ROM SHA-256: {SHA256}")
        for entry in entries:
            print(f"{entry['handler']}  {entry['sequence']}  " + ", ".join(entry["presses"]))
        print(f"{len(entries)} button cheats; directions are D-Pad inputs.")


if __name__ == "__main__":
    main()
