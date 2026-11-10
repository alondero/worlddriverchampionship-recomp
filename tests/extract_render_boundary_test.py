"""Extract the race draw boundary and its persistent-state helpers unchanged.

Rendering, task submission and audio processing are observed test doubles.
The fixture does not claim that the complete draw graph is side-effect free.
"""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
roots = {"func_8000A30C", "func_80011BB8", "func_80011BA0",
         "func_8001F9CC", "func_80025350"}
natives = {"func_80001D34", "func_80024038", "func_80010A5C",
           "func_8004D020", "func_80044FA8", "func_80046D20",
           "func_80024094", "func_80024D38", "func_800245AC",
           "func_8002430C", "func_80010ECC", "func_8004D120",
           "func_80044FC8", "func_80046D40", "func_80024364",
           "func_800241C0", "func_80024FCC", "func_800214C8"}
functions = {}
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        if match[1] in roots:
            functions[match[1]] = match[0]
if roots - functions.keys():
    raise SystemExit(f"Missing translated bodies: {roots - functions.keys()}")
calls = set()
for body in functions.values():
    if "LOOKUP_FUNC" in body:
        raise SystemExit("Unexpected indirect render-boundary call")
    calls.update(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
if calls - roots - natives:
    raise SystemExit(f"Unexpected callees: {calls - roots - natives}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(roots | calls)) + "\n" +
    "\n".join(functions[name] for name in sorted(roots)), encoding="utf-8")
