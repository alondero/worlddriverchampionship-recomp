"""Build separate production and frozen USA camera-update test closures."""
import hashlib
import json
import re
import struct
import subprocess
import sys
import tomllib
from pathlib import Path


UPDATE = "func_80012354"
BLEND = "func_8001A4C4"
DISPATCH_CALL = "func_8000F678"
ROOTS = (DISPATCH_CALL, BLEND, "func_8000FAE8")
FROZEN_HOOKS = (
    (UPDATE, 0x80012354, "camera_reference_begin"),
    (UPDATE, 0x80012870, "camera_reference_end"),
    (BLEND, 0x8001A4C4, "camera_reference_rate"),
)


def check_rom_edges(rom, analysis):
    """Check the bounded static evidence used by the caller/observer fixtures."""
    start, end = 0x80012354, 0x80012884
    bias = analysis["entrypoint"] - analysis["section_rom"]
    code_start, code_end = analysis["code_rom_start"], analysis["code_rom_end"]
    update_calls, blend_calls = [], []
    for offset in range(code_start, code_end, 4):
        address = offset + bias
        word, = struct.unpack_from(">I", rom, offset)
        op = word >> 26
        if op in (2, 3):
            target = ((address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        elif (op in (4, 5, 6, 7, 20, 21, 22, 23) or
              (op == 1 and ((word >> 16) & 31) in (0, 1, 2, 3, 16, 17, 18, 19)) or
              (op == 17 and ((word >> 21) & 31) == 8)):
            target = address + 4 + 4 * ((word & 0x7FFF) - (word & 0x8000))
        else:
            continue
        if op == 3 and target == 0x8001A4C4:
            blend_calls.append(address)
        if start <= target < end and not start <= address < end:
            if target != start or op != 3:
                raise SystemExit(f"Extra entry into camera update from {address:#x} to {target:#x}")
            update_calls.append(address)
    if update_calls != [0x8000F684, 0x8000FBC0] or blend_calls != [0x800124E8]:
        raise SystemExit(f"Unexpected direct camera/blend calls: {update_calls}, {blend_calls}")
    if any(start <= word < end or word == 0x8001A4C4 for word, in struct.iter_unpack(">I", rom)):
        raise SystemExit("ROM contains an aligned pointer word into the camera update or blend")


def generate_reference(root, recompiler, work):
    analysis = tomllib.loads((root / "rom-analysis.toml").read_text(encoding="utf-8"))["rom"]
    rom = root / analysis["filename"]
    data = rom.read_bytes()
    if len(data) != analysis["rom_size"] or hashlib.sha256(data).hexdigest() != analysis["sha256"]:
        raise SystemExit("Camera update reference requires the reviewed USA ROM")
    check_rom_edges(data, analysis)

    # Keep the section mapping, symbol sizes, other runtime hooks and
    # instruction transformations. Freeze policy at these two bodies so later
    # production changes cannot alter the reference or call its implementations.
    work.mkdir(exist_ok=True)
    symbols = work / "symbols.toml"
    symbols.write_bytes((root / "worlddriverchampionship.syms.toml").read_bytes())
    config = (root / "worlddriverchampionship.us.toml").read_text(encoding="utf-8")
    for setting, path in {
        "output_func_path": work / "functions",
        "symbols_file_path": symbols,
        "rom_file_path": rom,
    }.items():
        config, count = re.subn(rf"^{setting} = .+$",
                                lambda _: f"{setting} = {json.dumps(path.as_posix())}", config, flags=re.M)
        if count != 1:
            raise SystemExit(f"Expected exactly one {setting} in ROM config")
    # The frozen bodies have no instruction replacements. Keep transformations
    # in their callees and other bodies from the production configuration.
    config = re.sub(r'(?ms)^\[\[patches\.(?:hook|instruction)\]\]\n'
                    rf'func = "(?:{UPDATE}|{BLEND})"\n.*?(?=^\[\[|\Z)', "", config)
    for name, address, hook in FROZEN_HOOKS:
        config += (f'\n[[patches.hook]]\nfunc = "{name}"\nbefore_vram = {address:#x}\n'
                   f"text = '{{ extern void {hook}(uint8_t*, recomp_context*); {hook}(rdram, ctx); }}'\n")
    config += (f'\n[[patches.hook]]\nfunc = "{UPDATE}"\nbefore_vram = 0x800124e8\n'
               "text = 'ctx->r31 = (int32_t)0x800124F0u;'\n")
    config_path = work / "config.toml"
    config_path.write_text(config, encoding="utf-8")
    subprocess.run([str(recompiler), str(config_path)], cwd=work, check=True)
    return work / "functions"


def extract(source, destination, reference):
    functions = {}
    for path in sorted(source.glob("*.c")):
        for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                                 path.read_text(encoding="utf-8"), re.S):
            if match[1] in functions:
                raise SystemExit(f"Duplicate translated function {match[1]}")
            functions[match[1]] = match[0]

    # Execute the actual argument loads and JAL delay slot in the dispatcher.
    # The fixture supplies its two saved arguments and stops after the call.
    dispatch = functions.get("func_8000E1F4", "")
    call = re.search(r"(?ms)^L_8000F678:\n(.+?)^    // 0x8000F68C:", dispatch)
    if not call or call[1].count(f"{UPDATE}(rdram, ctx);") != 1:
        raise SystemExit("Expected one translated camera dispatch call at 8000F684")
    functions[DISPATCH_CALL] = (f"RECOMP_FUNC void {DISPATCH_CALL}(uint8_t* rdram, recomp_context* ctx) {{\n"
                                f"{call[1]}    return;\n}}\n")

    host_hooks = {hook for _, _, hook in FROZEN_HOOKS} if reference else set()
    pending, closure = list(ROOTS), set()
    while pending:
        name = pending.pop()
        if name in closure or name in host_hooks:
            continue
        if name not in functions or "LOOKUP_FUNC" in functions[name]:
            raise SystemExit(f"Camera update test requires a direct ROM body for {name}")
        closure.add(name)
        pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))

    if reference and any(re.search(r"\bwdc_\w+\b", functions[name]) for name in closure):
        raise SystemExit("Frozen camera closure may call only its own policy hooks")
    names = {name: f"reference_{name}" if reference else name for name in closure}
    replace_name = re.compile(r"\b(?:" + "|".join(sorted(closure)) + r")\b")
    output = ['#include "recomp.h"',
              'void camera_update_trace(int reference, uint32_t address, uint8_t*, recomp_context*);']
    output.extend(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(names.values()))
    for name in sorted(closure):
        body = replace_name.sub(lambda match: names[match[0]], functions[name])
        signature, rest = body.split("{", 1)
        trace = f"camera_update_trace({int(reference)}, 0x{name.removeprefix('func_')}u, rdram, ctx);"
        if reference and name == BLEND:
            # Compare the register inputs consumed by the shared arithmetic.
            # The frozen image applies its weight inside the helper's hook.
            hook_call = "camera_reference_rate(rdram, ctx);"
            if rest.count(hook_call) != 1:
                raise SystemExit("Expected one frozen camera blend policy hook")
            rest = rest.replace(hook_call, f"{hook_call} {trace}")
        else:
            rest = f"\n    {trace}{rest}"
        output.append(f"{signature}{{{rest}")
    destination.write_text("\n".join(output), encoding="utf-8")


if __name__ == "__main__":
    root, recompiler, build = (Path(arg).resolve() for arg in sys.argv[1:])
    reference = generate_reference(root, recompiler, build / "camera-update-reference")
    extract(reference, build / "camera_update_reference.c", reference=True)
    extract(root / "RecompiledFuncs", build / "camera_update_production.c", reference=False)
