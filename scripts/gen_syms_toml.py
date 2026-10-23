#!/usr/bin/env python3
"""Generate a first-pass runtime-addressed symbol/config pair from ROM bytes.

This is deliberately a measurement aid, not an automatic decompiler. The
code range and ROM identity live in rom-analysis.toml and must be confirmed
from the actual WDC dump before generated output is trusted.
"""
from __future__ import annotations

import re
import hashlib
import bisect
import struct
import sys
import tomllib
from pathlib import Path
from rom_transforms import expand_cache_markers

ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "rom-analysis.toml"
FORCE_STUB = ROOT / "force_stub.txt"
OUT_SYMS = ROOT / "worlddriverchampionship.syms.toml"
OUT_CONFIG = ROOT / "worlddriverchampionship.us.toml"


def read_analysis() -> dict[str, int | str]:
    values: dict[str, int | str] = {}
    for line in ANALYSIS.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line or "=" not in line:
            continue
        key, raw = (part.strip() for part in line.split("=", 1))
        if raw.startswith('"') and raw.endswith('"'):
            values[key] = raw[1:-1]
        else:
            try:
                values[key] = int(raw, 0)
            except ValueError:
                pass
    required = ("filename", "entrypoint", "section_rom", "code_rom_start",
                "code_rom_end", "rom_size")
    missing = [key for key in required if key not in values]
    if missing:
        raise SystemExit(f"rom-analysis.toml is missing: {', '.join(missing)}")
    if any(int(values[key]) == 0 for key in required[1:]):
        raise SystemExit("fill the TODO values in rom-analysis.toml from ROM evidence first")
    return values


def load_force_stubs() -> list[str]:
    return [
        line.strip() for line in FORCE_STUB.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]


def main() -> int:
    config = read_analysis()
    rom_path = ROOT / str(config["filename"])
    if not rom_path.is_file():
        raise SystemExit(f"ROM not found: {rom_path}")
    rom = rom_path.read_bytes()
    expected_size = int(config["rom_size"])
    if len(rom) != expected_size:
        raise SystemExit(f"ROM size is 0x{len(rom):X}; analysis expects 0x{expected_size:X}")
    if rom[:4] != bytes.fromhex("80371240"):
        raise SystemExit(f"ROM is not a big-endian .z64 (header={rom[:4].hex()})")
    if config.get("sha256") != hashlib.sha256(rom).hexdigest():
        raise SystemExit("ROM SHA-256 does not match the reviewed USA image")

    entry = int(config["entrypoint"])
    section_rom = int(config["section_rom"])
    code_start = int(config["code_rom_start"])
    code_end = int(config["code_rom_end"])
    if not (section_rom <= code_start < code_end <= len(rom)) or code_start % 4 or code_end % 4:
        raise SystemExit("code range must be aligned and inside the ROM")

    def rom_to_vram(offset: int) -> int:
        return entry + offset - section_rom

    symbol_config = tomllib.loads((ROOT / "rom-symbols.toml").read_text(encoding="utf-8"))
    rom, transformed = expand_cache_markers(rom, symbol_config["boundaries"].get("cache_transform", []), entry - section_rom)
    names = {int(k, 0): v for k, v in symbol_config["symbols"].items()}
    natives = {int(k, 0): v for k, v in symbol_config.get("native", {}).items()}
    names.update(natives)
    starts = {entry, *names, *symbol_config["boundaries"]["extra"]}
    for offset in range(code_start, code_end, 4):
        word = struct.unpack_from(">I", rom, offset)[0]
        if word >> 26 == 0x03:  # jal
            target = ((word & 0x03FFFFFF) << 2) | (entry & 0xF0000000)
            target_rom = section_rom + target - entry
            if code_start <= target_rom < code_end and target_rom % 4 == 0:
                starts.add(target)
        # addiu $sp,$sp,-N: the most common IDO function prologue.
        if (word >> 26) == 0x09 and ((word >> 21) & 31) == 29 and ((word >> 16) & 31) == 29:
            previous = struct.unpack_from(">I", rom, offset - 4)[0]
            before_delay = struct.unpack_from(">I", rom, offset - 8)[0]
            if word & 0x8000 and (previous == 0 or before_delay & 0xFC00003F == 8):
                starts.add(rom_to_vram(offset))

    starts.difference_update(symbol_config["boundaries"]["omit"])
    sorted_starts = sorted(starts)
    functions: list[tuple[str, int, int]] = []
    for index, vram in enumerate(sorted_starts):
        offset = section_rom + vram - entry
        next_offset = (section_rom + sorted_starts[index + 1] - entry
                       if index + 1 < len(sorted_starts) else code_end)
        size = symbol_config.get("sizes", {}).get(f"0x{vram:08X}", next_offset - offset)
        # Reviewed assembly adapters enter the next callable body by fallthrough.
        # Keep that body's range, rather than emitting an empty host return.
        if vram in symbol_config["boundaries"].get("fallthrough", []):
            end_index = index + 2
            size = (sorted_starts[end_index] if end_index < len(sorted_starts)
                    else rom_to_vram(code_end)) - vram
        # Preserve forward shared assembly tails. Multiple callable entries can
        # overlap; retaining both is necessary for indirect function pointers.
        cursor = offset
        while cursor < offset + size:
            word = struct.unpack_from(">I", rom, cursor)[0]
            op = word >> 26
            branch = op in (4, 5, 6, 7, 20, 21, 22, 23) or (op == 1 and (word >> 16) & 31 in (0, 1, 2, 3, 16, 17, 18, 19)) or (op == 17 and (word >> 21) & 31 == 8)
            if branch:
                delta = (word & 0x7FFF) - (word & 0x8000)
                target = rom_to_vram(cursor) + 4 + delta * 4
                if vram + size <= target < rom_to_vram(code_end):
                    upper = bisect.bisect_right(sorted_starts, target)
                    end = sorted_starts[upper] if upper < len(sorted_starts) else rom_to_vram(code_end)
                    size = end - vram
            cursor += 4
        if size > 0 and size % 4 == 0:
            name = names.get(vram, "recomp_entrypoint" if vram == entry else f"func_{vram:08X}")
            functions.append((name, vram, size))

    section_size = code_end - section_rom
    syms = [
        "# Autogenerated by scripts/gen_syms_toml.py -- DO NOT EDIT BY HAND.",
        "# First-pass jal/prologue scan. Review every boundary against ROM evidence.",
        "[[section]]", 'name = ".text"', f"rom = {section_rom:#x}",
        f"vram = {entry:#x}", f"size = {section_size:#x}", "", "functions = [",
    ]
    syms.extend(f'    {{ name = "{name}", vram = {vram:#x}, size = {size:#x} }},'
                for name, vram, size in functions)
    syms.append("]")
    OUT_SYMS.write_text("\n".join(syms) + "\n", encoding="utf-8", newline="\n")

    stubs = load_force_stubs()
    stubs_toml = "[]" if not stubs else "[\n" + ",\n".join(f'    "{name}"' for name in stubs) + ",\n]"
    output = [
        "# Autogenerated by scripts/gen_syms_toml.py -- DO NOT EDIT BY HAND.",
        "[input]", f"entrypoint = {entry:#x}", 'output_func_path = "RecompiledFuncs"',
        'symbols_file_path = "worlddriverchampionship.syms.toml"',
        f'rom_file_path = "{config["filename"]}"', "", "[patches]",
        f"stubs = {stubs_toml}", "ignored = []", "",
    ]
    for hook in symbol_config.get("hooks", []):
        output.extend(['[[patches.hook]]', f'func = "{hook["func"]}"',
                       f'before_vram = {hook["before_vram"]:#x}',
                       f'text = \'{hook["text"]}\'', ''])
    for patch in symbol_config.get("instructions", []):
        address = patch["vram"]
        if not any(name == patch["func"] and start <= address < start + size
                   for name, start, size in functions) or address % 4:
            raise SystemExit(f"Instruction patch outside function: {address:#x}")
        word = struct.unpack_from(">I", rom, section_rom + address - entry)[0]
        if word != patch["expected"]:
            raise SystemExit(f"Instruction patch mismatch at {address:#x}: {word:#x}")
        output.extend(['[[patches.instruction]]', f'func = "{patch["func"]}"',
                       f'vram = {address:#x}', f'value = {patch["value"]:#x}', ''])
    # Cache maintenance is redundant with coherent host RDRAM. Preserve the
    # surrounding guest computation, including the game's custom assembly.
    # Privileged CPU helpers cannot execute on the host; until identified and
    # replaced, make their entry fail loudly rather than silently no-op.
    privileged = list(symbol_config["boundaries"].get("unimplemented", []))
    for name, vram, size in functions:
        if vram in names:
            continue
        for address in range(vram, vram + size, 4):
            word = struct.unpack_from(">I", rom, section_rom + address - entry)[0]
            if word >> 26 == 0x2F:
                output.extend(['[[patches.instruction]]', f'func = "{name}"',
                               f'vram = {address:#x}', 'value = 0', ''])
            elif address in transformed:
                output.extend(['[[patches.instruction]]', f'func = "{name}"',
                               f'vram = {address:#x}', f'value = {word:#x}', ''])
            elif word >> 26 == 0x10:
                privileged.append(name)
                break
    # These bodies belong to the original exception/TLB kernel. Native OS
    # replacements must bypass them. The host definitions abort if reached.
    privileged = sorted(set(privileged))
    output[output.index("ignored = []")] = "ignored = [" + ", ".join(f'"{n}"' for n in privileged + list(natives.values())) + "]"
    trap_source = ['// Generated by scripts/gen_syms_toml.py.', '#include "recomp.h"',
                   'extern void wdc_unimplemented(const char* name);']
    trap_source.extend(f'void {name}(uint8_t* rdram, recomp_context* ctx) {{ wdc_unimplemented("{name}"); }}'
                       for name in privileged)
    (ROOT / "RecompiledFuncs").mkdir(exist_ok=True)
    (ROOT / "RecompiledFuncs" / "wdc_privileged.c").write_text("\n".join(trap_source) + "\n", encoding="utf-8")
    OUT_CONFIG.write_text("\n".join(output), encoding="utf-8", newline="\n")
    print(f"ROM: {rom_path.name} ({len(rom):#x} bytes)")
    print(f"functions: {len(functions)}; text: {code_start:#x}..{code_end:#x}")
    print(f"wrote {OUT_SYMS.name} and {OUT_CONFIG.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
