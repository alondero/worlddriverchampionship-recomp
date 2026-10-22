"""Extract the ROM menu band and separator-bar builders with their generated hooks."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
roots = {"func_800500E0", "func_8005063C"}
# Sprite blits and overlay predicates are outside the bracket placement under test.
natives = {"func_8006DA18", "func_8006CCD4", "func_800664F4", "func_80066558",
           "func_80051028", "wdc_menu_fill_begin", "wdc_menu_fill_end"}
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
    calls.update(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
if calls - natives:
    raise SystemExit(f"Unexpected callees: {calls - natives}")
for hook in ("wdc_menu_fill_begin", "wdc_menu_fill_end"):
    if hook not in calls:
        raise SystemExit(f"Generated menu builders do not call {hook}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(roots | calls)) + "\n" +
    "\n".join(functions[name] for name in sorted(roots)), encoding="utf-8")
