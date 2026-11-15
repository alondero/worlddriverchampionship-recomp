"""Compile a read-only reference translation copy with one race draw harness seam."""
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess


def build_draw_boundary(reference, output):
    entries = json.loads((reference / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/RecompiledFuncs/funcs_3.c"))
    original = Path(entry["file"]).read_bytes()
    source = original.decode().replace("\r\n", "\n")
    match = re.search(r"RECOMP_FUNC void func_8000C560\(.*?(?=RECOMP_FUNC void |\Z)", source, re.S)
    if not match or match[0].count("func_8000A30C(rdram, ctx);") != 1:
        raise RuntimeError("Race draw boundary changed")
    body = match[0].replace("func_8000A30C(rdram, ctx);", "prototype_race_draw(rdram, ctx);")
    source = source[:match.start()] + body + source[match.end():]
    include = '#include "recomp.h"'
    if source.count(include) != 1:
        raise RuntimeError("Translation include seam changed")
    source = source.replace(include, include + '\nvoid prototype_race_draw(uint8_t*, recomp_context*);')
    copied = output / "prototype_race_funcs.c"
    copied.write_text(source)
    obj = output / "prototype_race_funcs.o"
    command = shlex.split(entry["command"])
    # Preserve the reference C compiler and flags rather than compiling generated C as C++.
    cache = (reference / "CMakeCache.txt").read_text()
    command[0] = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.*)$", cache, re.M)[1]
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(copied)
    command.extend(["-I", str(Path(entry["file"]).parent)])
    subprocess.run(command, cwd=entry["directory"], check=True)
    (output / "draw-boundary-source.json").write_text(json.dumps({
        "source": entry["file"], "source_sha256": hashlib.sha256(original).hexdigest(),
        "copied_sha256": hashlib.sha256(copied.read_bytes()).hexdigest(),
        "compile_command": command,
    }, indent=2))
    return str(obj)
