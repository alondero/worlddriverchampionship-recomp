"""Shared helpers for big-endian N64 ROM analysis tools."""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ROM = ROOT / "World Driver Championship (USA).z64"
SYMS_TOML = ROOT / "worlddriverchampionship.syms.toml"
ANALYSIS_TOML = ROOT / "rom-analysis.toml"


def _analysis_int(key: str) -> int:
    match = re.search(rf"^\s*{re.escape(key)}\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)",
                      ANALYSIS_TOML.read_text(encoding="utf-8"), re.MULTILINE)
    return int(match.group(1), 0) if match else 0


VRAM_BASE = _analysis_int("entrypoint") or 0x80000400
ROM_TEXT_START = _analysis_int("section_rom") or 0x1000
ROM_TEXT_END = _analysis_int("code_rom_end")


def vram_to_rom(vram: int) -> int:
    return ROM_TEXT_START + vram - VRAM_BASE


def rom_to_vram(offset: int) -> int:
    return VRAM_BASE + offset - ROM_TEXT_START


def load_rom(path=None) -> bytes:
    rom_path = Path(path) if path else DEFAULT_ROM
    if not rom_path.exists():
        sys.exit(f"ROM not found: {rom_path}")
    data = rom_path.read_bytes()
    if data[:4] != bytes.fromhex("80371240"):
        sys.exit(f"{rom_path} is not a big-endian .z64")
    return data


_FUNC_RE = re.compile(r'\{\s*name\s*=\s*"([^"]+)"\s*,\s*vram\s*=\s*(0x[0-9a-fA-F]+)\s*,\s*size\s*=\s*(0x[0-9a-fA-F]+)')


def load_functions():
    if not SYMS_TOML.exists():
        sys.exit("generated symbol table missing; run scripts/gen_syms_toml.py first")
    functions = [(int(m.group(2), 16), int(m.group(3), 16), m.group(1))
                 for m in _FUNC_RE.finditer(SYMS_TOML.read_text(encoding="utf-8"))]
    return sorted(functions)


def containing_function(functions, vram: int):
    for function in functions:
        if function[0] <= vram < function[0] + function[1]:
            return function
    return None


def lookup(functions, name_or_addr: str):
    for function in functions:
        if function[2] == name_or_addr.strip():
            return function
    try:
        address = int(name_or_addr, 16)
    except ValueError:
        sys.exit(f"unknown function or address: {name_or_addr}")
    if address < 0x80000000:
        address |= 0x80000000
    function = containing_function(functions, address)
    if function is None:
        sys.exit(f"address is outside known functions: 0x{address:08X}")
    return function


def word_at(rom: bytes, offset: int) -> int:
    return struct.unpack_from(">I", rom, offset)[0]
