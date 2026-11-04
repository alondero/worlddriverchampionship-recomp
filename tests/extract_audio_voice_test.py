"""Extract the ROM's voice-update seam; synthesis is outside this fixture."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
names = {"func_80043A30", "func_80042A44", "func_80042A00", "func_80042880"}
functions = {}
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        if match[1] in names:
            functions[match[1]] = match[0]
if functions.keys() != names:
    raise SystemExit("Missing translated audio voice function")
calls = set(re.findall(r"\b(\w+)\(rdram, ctx\);", "\n".join(functions.values())))
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {n}(uint8_t*, recomp_context*);" for n in sorted(calls)) + "\n" +
    "\n".join(functions[n] for n in sorted(names)), encoding="utf-8")
