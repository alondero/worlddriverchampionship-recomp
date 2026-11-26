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



def visibility_list_patches(
    rom: bytes, functions: list[tuple[str, int, int]], entry: int, section_rom: int,
    original_address: int, replacement_address: int, original_size: int = 0x100,
) -> list[dict]:
    """Measure simple LUI/%LO references to the visible-section list.

    The replacement deliberately keeps the original low 16 bits. That makes
    each reviewed relocation a one-instruction LUI high-half patch; the low
    half may participate in either full-pointer arithmetic or a memory base.
    Unsupported shapes fail generation instead of silently leaving a mixed
    original/expanded list implementation.
    """
    if (original_address & 0xffff) != (replacement_address & 0xffff):
        raise ValueError("visibility list relocation must preserve the low 16 bits")
    original_hi = ((original_address + 0x8000) >> 16) & 0xffff
    replacement_hi = ((replacement_address + 0x8000) >> 16) & 0xffff
    lo = original_address & 0xffff
    lo_end = lo + original_size
    if lo_end > 0x10000:
        raise ValueError("original visibility list range must not cross a 64 KiB boundary")
    immediate_ops = {0x08, 0x09, 0x0c, 0x0d, 0x0e}  # addi/addiu/andi/ori/xori
    memory_ops = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                  0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x31, 0x35, 0x39, 0x3d}

    def uses_base(word: int, reg: int) -> bool:
        op = word >> 26
        rs = (word >> 21) & 31
        return rs == reg and (op in immediate_ops or op in memory_ops)

    patches: list[dict] = []
    required = {"func_8002343C", "func_800218F0"}
    for name, vram, size in functions:
        if name not in required:
            continue
        required.remove(name)
        start = section_rom + vram - entry
        end = start + size
        if start < 0 or end > len(rom) or start % 4 or size % 4:
            raise ValueError(f"visibility list function range is invalid for {name}")
        found = 0
        for offset in range(start, end - 4, 4):
            word = struct.unpack_from(">I", rom, offset)[0]
            if word >> 26 != 0x0f or (word & 0xffff) != original_hi:
                continue
            reg = (word >> 16) & 31
            next_word = struct.unpack_from(">I", rom, offset + 4)[0]
            if not lo <= (next_word & 0xffff) < lo_end:
                continue
            if not uses_base(next_word, reg):
                site = vram + offset - start
                raise ValueError(f"unsupported visibility list reference shape at {site:#x}")
            site = vram + offset - start
            patches.append({
                "func": name,
                "vram": site,
                "expected": word,
                "value": (word & 0xffff0000) | replacement_hi,
            })
            found += 1
        if not found:
            raise ValueError(f"no measured visibility list reference in {name}")
    if required:
        raise ValueError(f"missing visibility list function: {sorted(required)[0]}")
    return patches


def link_register_hooks(
    rom: bytes, targets: list[int], functions: list[tuple[str, int, int]],
    code_start: int, code_end: int, mapping_delta: int,
    existing_hooks: list[dict] = (),
) -> list[dict]:
    """Restore MIPS JAL's link for reviewed caller-sensitive leaf routines.

    The pinned recompiler normally emits host calls without updating r31.
    Every direct call to these targets needs the link, including save and
    progression callers that must not inherit a previous menu caller's link.
    Hooks run before JAL, so its delay slot also observes the correct value.
    Overlapping translated bodies each receive their own hook. Unreviewed
    indirect/tail entries or an existing hook at the same site fail closed.
    """
    selected = set(targets)
    if not selected:
        return []
    if not (0 <= code_start < code_end <= len(rom)) or code_start % 4 or code_end % 4:
        raise ValueError("link-register code range must be aligned and inside the ROM")
    entries = {start for _, start, _ in functions}
    if not selected <= entries:
        raise ValueError("link-register target lacks a reviewed function entry")

    # No selected accessor is a ROM function-pointer target. A future table
    # reference needs an explicit indirect-caller policy before regeneration.
    for index, (word,) in enumerate(struct.iter_unpack(">I", memoryview(rom)[:len(rom) // 4 * 4])):
        if word in selected:
            raise ValueError(f"unreviewed link-register function pointer at ROM {index * 4:#x}")

    occupied = {(hook["func"], hook["before_vram"]) for hook in existing_hooks}
    hooks = []
    called = set()
    for offset in range(code_start, code_end, 4):
        word = struct.unpack_from(">I", rom, offset)[0]
        if word >> 26 not in (2, 3):  # j / jal
            continue
        address = offset + mapping_delta
        target = ((address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        if target not in selected:
            continue
        if word >> 26 == 2:
            raise ValueError(f"unreviewed tail entry to link-register target at {address:#x}")
        if offset + 8 > code_end:
            raise ValueError(f"link-register call lacks its delay slot at {address:#x}")
        owners = [(name, start, size) for name, start, size in functions
                  if start <= address < start + size]
        if not owners or any(address + 8 > start + size for _, start, size in owners):
            raise ValueError(f"link-register call is outside complete function bodies at {address:#x}")
        called.add(target)
        for name, _, _ in owners:
            if (name, address) in occupied:
                raise ValueError(f"link-register hook conflicts with {name} at {address:#x}")
            hooks.append({"func": name, "before_vram": address,
                          "text": f"ctx->r31 = (int32_t)0x{address + 8:08X}u;"})
    if called != selected:
        raise ValueError("link-register target has no measured direct callers")
    return hooks


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
    instructions = list(symbol_config.get("instructions", []))
    visibility = symbol_config.get("visibility", {})
    if visibility:
        instructions.extend(visibility_list_patches(
            rom, functions, entry, section_rom,
            visibility["list_address"], visibility["expanded_list_address"],
            visibility.get("original_list_bytes", 0x100)))
    # An instruction replacement may supply the translated operation at that
    # same address, including when the instruction occupies a delay slot.
    hooks = symbol_config.get("hooks", []) + [
        {"func": patch["func"], "before_vram": patch["vram"], "text": patch["text"]}
        for patch in instructions if "text" in patch
    ]
    for call in symbol_config.get("render_swap_calls", []):
        jal = 0x0C000000 | ((call["target"] >> 2) & 0x03FFFFFF)
        for site in call["sites"]:
            # Execute the original argument setup before calling the bridge.
            # Both words are checked against the ROM by the patch emitter.
            instructions.extend([
                dict(func=call["func"], vram=site, expected=jal, value=call["delay"]),
                dict(func=call["func"], vram=site + 4, expected=call["delay"], value=0),
            ])
            bridge = call["bridge"]
            hooks.append(dict(func=call["func"], before_vram=site + 4,
                              text=f"{{ extern void {bridge}(uint8_t*, recomp_context*); {bridge}(rdram, ctx); }}"))
    try:
        hooks = hooks + link_register_hooks(
            rom, symbol_config.get("link_register_targets", []), functions,
            code_start, code_end, entry - section_rom, hooks)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    for hook in hooks:
        output.extend(['[[patches.hook]]', f'func = "{hook["func"]}"',
                       f'before_vram = {hook["before_vram"]:#x}',
                       f'text = \'{hook["text"]}\'', ''])
    for patch in instructions:
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
