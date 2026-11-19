"""Recompile the accepted ROM in isolation for the projection differential test."""
import hashlib
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path


ENTRY = "func_80011D54"
CALLS = ("func_8001A864", "func_80070DB0", "func_8001A79C", "func_800710E0", "func_8002CF30")
# Freeze the old injection sites even after they disappear from the production
# inputs. Each function is implemented independently of the recovered policy.
HOOKS = {
    0x80011DC4: "projection_reference_aspect",
    0x80011DE8: "projection_reference_distance",
    0x80011DFC: "projection_reference_normalize",
}
BODY = re.compile(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)", re.S)
CALL = re.compile(r"\b(\w+)\(rdram, ctx\);")
HOOK = re.compile(r"(?ms)^\[\[patches\.hook\]\]\n.*?(?=^\[\[|\Z)")


def declaration(name):
    return f"void {name}(uint8_t*, recomp_context*);\n"


def read_functions(folder):
    return {match[1]: match[0] for path in sorted(folder.glob("*.c"))
            for match in BODY.finditer(path.read_text(encoding="utf-8"))}


def closure(functions, entry, boundaries=()):
    bodies, pending = {}, [entry]
    while pending:
        name = pending.pop()
        if name in bodies or name in boundaries:
            continue
        body = functions.get(name, "")
        if not body or "LOOKUP_FUNC" in body:
            raise SystemExit(f"Expected a direct projection closure body for {name}")
        bodies[name] = body
        pending.extend(CALL.findall(body))
    return bodies


def instrument(body, names):
    return CALL.sub(lambda m: f"{names[m[1]]}(rdram, ctx);" if m[1] in names else m[0], body)


def wrappers(names, implementations):
    return "".join(
        f"void {name}(uint8_t* rdram, recomp_context* ctx) {{\n"
        f"    projection_test_call(rdram, ctx, {index}, {implementation});\n"
        "}\n"
        for index, (name, implementation) in enumerate(zip(names, implementations, strict=True))
    )


def translate(work, root, recompiler, rom, variant):
    entry = f"projection_reference_{variant}"
    work.mkdir(parents=True, exist_ok=True)
    symbols = (root / "worlddriverchampionship.syms.toml").read_text(encoding="utf-8")
    if symbols.count(f'name = "{ENTRY}"') != 1:
        raise SystemExit("Projection reference requires one original entry")
    symbol_path = work / "projection.syms.toml"
    symbol_path.write_text(symbols.replace(f'name = "{ENTRY}"', f'name = "{entry}"'), encoding="utf-8")
    config = (root / "worlddriverchampionship.us.toml").read_text(encoding="utf-8")
    # Keep every setting and patch outside the projection entry. Projection
    # instruction patches, if present, still apply under the reference name.
    config = HOOK.sub(lambda m: "" if tomllib.loads(m[0])["patches"]["hook"][0]["func"] == ENTRY
                      else m[0], config)
    # The player supplies a native entry. Translate the original symbol under
    # its independent name and keep every other ignored boundary.
    ignored = [name for name in tomllib.loads(config)["patches"]["ignored"] if name != ENTRY]
    config, count = re.subn(r"(?m)^ignored = .*?$", lambda _: f"ignored = {json.dumps(ignored)}", config)
    if count != 1:
        raise SystemExit("Projection reference expected one ignored list")
    config = config.replace(f'func = "{ENTRY}"', f'func = "{entry}"')
    for key, path in (("output_func_path", work / "functions"),
                      ("symbols_file_path", symbol_path), ("rom_file_path", rom)):
        config, count = re.subn(rf'(?m)^{key} = "[^"\n]*"$',
                                lambda _: f"{key} = {json.dumps(path.as_posix(), ensure_ascii=False)}", config)
        if count != 1:
            raise SystemExit(f"Projection reference expected one {key} setting")
    if variant == "enhanced":
        for address, hook in HOOKS.items():
            config += (f'\n[[patches.hook]]\nfunc = "{entry}"\nbefore_vram = {address:#x}\n'
                       f"text = '{{ extern void {hook}(uint8_t*, recomp_context*); {hook}(rdram, ctx); }}'\n")
    config_path = work / "projection.toml"
    config_path.write_text(config, encoding="utf-8")
    subprocess.run([str(recompiler), str(config_path)], cwd=work, check=True)
    bodies = closure(read_functions(work / "functions"), entry, HOOKS.values())
    calls = tuple(name for name in CALL.findall(bodies[entry]) if name not in HOOKS.values())
    if calls != CALLS:
        raise SystemExit(f"Unexpected projection call sequence: {calls}")
    return entry, bodies


def main():
    root, recompiler, destination = (Path(arg).resolve() for arg in sys.argv[1:])
    analysis = tomllib.loads((root / "rom-analysis.toml").read_text(encoding="utf-8"))["rom"]
    rom = root / analysis["filename"]
    if hashlib.sha256(rom.read_bytes()).hexdigest() != analysis["sha256"]:
        raise SystemExit("Projection reference requires the reviewed USA ROM")
    header = ('#include "recomp_compat.h"\n'
              'void projection_test_call(uint8_t*, recomp_context*, unsigned, recomp_func_t*);\n')
    reference = [header]
    for variant in ("stock", "enhanced"):
        entry, bodies = translate(destination / variant, root, recompiler, rom, variant)
        names = {name: f"projection_{variant}_{name}" for name in bodies if name != entry}
        traced = {name: f"projection_{variant}_call_{index}" for index, name in enumerate(CALLS)}
        bodies[entry] = instrument(bodies[entry], traced)
        reference.extend(declaration(name) for name in (*names.values(), *traced.values()))
        reference.extend(re.sub(r"\b\w+\b", lambda m: names.get(m[0], m[0]), body)
                         for body in bodies.values())
        reference.append(wrappers(traced.values(), (names[name] for name in CALLS)))
    (destination / "reference.c").write_text("".join(reference), encoding="utf-8")

    # The candidate invokes the normal production math and allocator bodies.
    # Renaming their definitions lets the host fixture observe both sides of
    # those calls without changing the bodies or inserting production hooks.
    functions = read_functions(root / "RecompiledFuncs")
    bodies = {}
    for name in CALLS:
        bodies.update(closure(functions, name))
    renames = {name: f"projection_test_{name}" for name in CALLS}
    layout_boundaries = {
        "func_8003C9A4", "func_8003C95C", "func_8000E020", "func_8000E18C",
        "func_8000E1BC", "func_8000F988", "func_80012024", "func_80012218",
        "func_800122F4", "func_8002A334", "wdc_remove_overscan",
    }
    for entry in ("func_800006E0", "func_8000FC18"):
        bodies.update(closure(functions, entry, layout_boundaries))
    candidate = [header, *(declaration(renames.get(name, name)) for name in bodies),
                 *(declaration(name) for name in sorted(layout_boundaries))]
    candidate.extend(re.sub(r"\b\w+\b", lambda m: renames.get(m[0], m[0]), body)
                     for body in bodies.values())
    candidate.append(wrappers(CALLS, renames.values()))
    (destination / "candidate_helpers.c").write_text("".join(candidate), encoding="utf-8")


if __name__ == "__main__":
    main()
