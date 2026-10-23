"""Extract the input-worker startup and real controller-ring initialization."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
roots = {"func_8002CB0C", "func_8002C494", "func_800036CC"}
natives = {"osCreateMesgQueue_recomp", "osSendMesg_recomp", "func_8004D79C",
           "osCreateThread_recomp", "osStartThread_recomp"}
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
        raise SystemExit("Unexpected indirect startup call")
    calls.update(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
if calls - roots - natives:
    raise SystemExit(f"Unexpected callees: {calls - roots - natives}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(roots | calls)) + "\n" +
    "\n".join(functions[name] for name in sorted(roots)), encoding="utf-8")
