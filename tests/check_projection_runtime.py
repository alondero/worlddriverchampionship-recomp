"""Run the pinned mod loader in isolation using synthetic code mods."""

import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


ROM = 0x1000
PROJECTION = 0x80011D54
CONTROL = 0x800006E0


def words(*values):
    return struct.pack(f"<{len(values)}I", *values)


def mod(case, name, target, kind, flags, digit, offline=False):
    folder = case / (name + ".offline.nrm" if offline else name)
    folder.mkdir(parents=True)
    # Append a digit to the test's marker in guest RAM and return.
    instructions = (0x3C088041, 0x8D090000, 0x00094900, 0x35290000 | digit,
                    0xAD090000, 0x03E00008, 0)
    binary = struct.pack(f">{len(instructions)}I", *instructions)
    (folder / "mod_binary.bin").write_bytes(binary)
    # Runtime mod symbol format v1. One section, one function, one operation.
    header = b"N64RSYMS" + words(1)
    counts = words(1, 0, 0, 0, int(kind == "replace"), 0, 0, 0, int(kind == "hook"), 0)
    section = words(0, 0, 0x81000000, len(binary), 0, 1, 0)
    function = words(0, len(binary))
    operation = words(0, ROM, target, flags)
    (folder / "mod_syms.bin").write_bytes(header + counts + section + function + operation)
    (folder / "mod.json").write_text(json.dumps({
        "game_id": "wdc-projection-test", "id": name, "display_name": name,
        "version": "1.0.0", "authors": ["fixture"], "minimum_recomp_version": "0.0.0",
    }), encoding="utf-8")


def main():
    executable, rom = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="wdc-projection-runtime-") as directory:
        root = Path(directory)
        mod(root / "entry-hook", "entry", PROJECTION, "hook", 0, 1)
        mod(root / "return-hook", "return", PROJECTION, "hook", 1, 3)
        mod(root / "replace-projection", "replace", PROJECTION, "replace", 0, 2)
        mod(root / "force-projection", "force", PROJECTION, "replace", 1, 2)
        mod(root / "offline-projection", "offline", PROJECTION, "replace", 0, 2, offline=True)
        mod(root / "hooked-projection", "entry", PROJECTION, "hook", 0, 1)
        mod(root / "hooked-projection", "force", PROJECTION, "replace", 1, 2)

        mod(root / "replace", "replace", CONTROL, "replace", 0, 2)
        mod(root / "force", "force", CONTROL, "replace", 1, 2)
        mod(root / "duplicate", "replace", CONTROL, "replace", 0, 2)
        mod(root / "duplicate", "force", CONTROL, "replace", 1, 2)
        mod(root / "hook-order", "entry", CONTROL, "hook", 0, 1)
        mod(root / "hook-order", "replace", CONTROL, "replace", 0, 2)
        mod(root / "hook-order", "return", CONTROL, "hook", 1, 3)
        mod(root / "offline-conflict", "entry", CONTROL, "hook", 0, 1)
        mod(root / "offline-conflict", "offline", CONTROL, "replace", 0, 2, offline=True)

        for case in sorted(root.iterdir()):
            print(case.name, flush=True)
            subprocess.run([executable, rom, "--runtime-mods", str(case)], check=True, timeout=15)


if __name__ == "__main__":
    main()
