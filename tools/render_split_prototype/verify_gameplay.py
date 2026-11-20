"""Compare 180 moving player-car transforms from one checkpoint in the MinGW player."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import tempfile


def build_fixture(build, out):
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    cache = (build / "CMakeCache.txt").read_text()
    cxx = re.search(r"^CMAKE_CXX_COMPILER:[^=]*=(.*)$", cache, re.M)[1]
    cc = re.search(r"^CMAKE_C_COMPILER:[^=]*=(.*)$", cache, re.M)[1]
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(e for e in entries if Path(e["file"]).name == "funcs_3.c")
    original = Path(entry["file"])
    source = original.read_text()
    # Copy just this translation unit. Observe the real physics call after
    # the original delay slot and before another queued sample can run.
    site = re.search(r"// 0x8000C6E8: jal\s+0x8000A5A0\b(?:(?!// 0x8000C6F0).)*?func_8000A5A0\(rdram, ctx\);", source, re.S)
    if not site:
        raise RuntimeError("USA physics call site changed")
    copied = out / "dispatch.c"
    copied.write_text(source[:site.end()] + "\n    { extern void compare_physics(uint8_t*); compare_physics(rdram); }" + source[site.end():])
    dispatch = out / "dispatch.o"
    args = shlex.split(entry["command"])
    args[0] = cc
    args[args.index("-o") + 1] = str(dispatch)
    args[args.index("-c") + 1] = str(copied)
    subprocess.run([*args, "-I", str(original.parent)], cwd=entry["directory"], check=True)
    observer = out / "observe.o"
    subprocess.run([cxx, "-std=c++20", "-O2", "-I", str(root / "lib/N64ModernRuntime/N64Recomp/include"),
                    "-I", str(root / "src"), "-c", str(here / "gameplay_trace.cpp"), "-o", str(observer)], check=True)
    ninja = (build / "build.ninja").read_text()
    block = re.search(r"^build worlddriverchampionship_modern\.exe: .*?(?=\n\n)", ninja, re.M | re.S)[0]
    inputs = block.splitlines()[0].split(": ", 1)[1].split(" |", 1)[0].split(" ", 1)[1]
    if "$" in inputs:
        raise RuntimeError("Unsupported escaped Ninja object path")
    link = [*shlex.split(inputs), str(observer), str(dispatch)]
    for key in ("LINK_PATH", "LINK_LIBRARIES"):
        match = re.search(r"^  " + key + " = (.*)$", block, re.M)
        if match:
            link.extend(shlex.split(match[1].replace("\\", "/")))
    exe = out / "gameplay_trace.exe"
    link.extend(["-Wl,--wrap=osContGetReadData_recomp", "-Wl,--wrap=wdc_render_race_draw", "-o", str(exe)])
    response = out / "link.rsp"
    response.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in link))
    subprocess.run([cxx, "@" + str(response)], cwd=build, check=True)
    for dll in build.glob("*.dll"):
        shutil.copy2(dll, out / dll.name)
    shutil.copytree(build / "assets", out / "assets")
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    args = parser.parse_args()
    build, rom = args.reference_build.resolve(strict=True), args.rom.resolve(strict=True)
    if hashlib.sha256(rom.read_bytes()).hexdigest() != "e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97":
        raise SystemExit("Expected the USA ROM")
    out = Path(tempfile.mkdtemp(prefix="wdc-gameplay-"))
    print(f"Gameplay evidence in {out}", flush=True)
    exe = build_fixture(build, out)
    slot = out / "practice.wstate"
    reference = None
    for name, rate in (("seed", 0), ("stock", 0), ("stock-repeat", 0), ("90hz", 90), ("144hz", 144)):
        run = out / name
        config = run / "build/user-data"
        config.mkdir(parents=True)
        (config / "graphics.json").write_text(json.dumps(dict(version=2, rate=rate, interpolate_races=bool(rate))))
        env = dict(os.environ, WDC_DIAGNOSTIC="1", WDC_WARP="1:0")
        for key in ("WDC_WARP_AT", "WDC_EXPERIMENTAL_DISPLAY_HZ", "WDC_STATE_SAVE", "WDC_STATE_LOAD"):
            env.pop(key, None)
        operation = "WDC_STATE_SAVE" if name == "seed" else "WDC_STATE_LOAD"
        env[operation], env[operation + "_DELAY"] = str(slot), "110"
        log = run / "run.log"
        with log.open("wb") as output:
            result = subprocess.run([str(exe), str(rom)], cwd=run, env=env, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=75)
        trace = log.read_text(errors="replace")
        if name == "seed":
            if result.returncode != 2 or "saved 8388608 bytes" not in trace or not slot.is_file():
                raise SystemExit(f"Checkpoint creation failed: {log}")
            continue
        if result.returncode or "loaded 8388608 bytes" not in trace:
            raise SystemExit(f"Checkpoint or simulation trace failed: {log}")
        if rate and not re.search(rf"display target {rate}/1 Hz, [1-9]\d* intermediate draws", trace):
            raise SystemExit(f"No interpolated race draw in {log}")
        data = (run / "physics.bin").read_bytes()
        rows = list(struct.iter_unpack("<17I", data))
        if len(rows) != 180 or any(b[0] != (a[0] + 1) % 2**32 for a, b in zip(rows, rows[1:])):
            raise SystemExit(f"Incomplete sample sequence: {log}")
        if len({row[13:16] for row in rows}) < 2:
            raise SystemExit(f"Player never moved: {log}")
        if reference is None:
            reference = rows
        if rows != reference:
            first = next(i for i, row in enumerate(rows) if row != reference[i])
            raise SystemExit(f"{name} differs at simulation sample {rows[first][0]}: {log}")
        print(f"{name}: samples {rows[0][0]}..{rows[-1][0]}, SHA-256 {hashlib.sha256(data).hexdigest()}", flush=True)


if __name__ == "__main__":
    main()
