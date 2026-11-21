"""Compile/run the real translated projection against a frozen ROM-backed fixture."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    args = parser.parse_args()
    rom = args.rom.resolve()
    if hashlib.sha256(rom.read_bytes()).hexdigest() != "e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97":
        raise SystemExit("Expected the verified USA ROM")
    cache = (args.reference_build / "CMakeCache.txt").read_text()
    source = Path(re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache, re.M)[1])
    cc = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.*)$", cache, re.M)[1]
    cxx = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.*)$", cache, re.M)[1]
    here = Path(__file__).resolve().parent
    output = here.parents[1] / "build/render-split-prototype"
    output.mkdir(parents=True, exist_ok=True)
    names = {"func_80070DB0", "func_80077390", "func_80071250", "func_80071410"}
    bodies = {}
    for path in sorted((source / "RecompiledFuncs").glob("*.c")):
        for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)", path.read_text(), re.S):
            if match[1] in names:
                bodies[match[1]] = match[0]
    if names != bodies.keys():
        raise SystemExit("Missing projection fixture functions")
    calls = set(re.findall(r"\b(\w+)\(rdram, ctx\);", "".join(bodies.values())))
    if calls - names or any("LOOKUP_FUNC" in body for body in bodies.values()):
        raise SystemExit("Unexpected projection dependencies")
    generated = output / "projection_funcs.c"
    generated.write_text('#include "recomp.h"\n' + "\n".join(
        f"void {name}(uint8_t*, recomp_context*);" for name in sorted(names)) +
        "\n" + "\n".join(bodies[name] for name in sorted(names)))
    include = source / "lib/N64ModernRuntime/N64Recomp/include"
    obj = output / "projection_funcs.o"
    exe = output / "context_test.exe"
    subprocess.run([cc, "-O2", "-fno-strict-aliasing", "-I", str(include), "-c", str(generated), "-o", str(obj)], check=True)
    subprocess.run([cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-fno-strict-aliasing", "-I", str(include),
                    "-I", str(here.parents[1]), str(here / "context_test.cpp"), str(obj), "-o", str(exe)], check=True)
    env = dict(os.environ)
    env["PATH"] = str(Path(cxx).parent) + os.pathsep + env.get("PATH", "")
    subprocess.run([str(exe), str(rom)], check=True, env=env)


if __name__ == "__main__":
    main()
