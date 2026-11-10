"""Extract the results drawer and real sprite coordinate/packet builders."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
roots = {"func_80046D40", "func_80051028", "func_80051114", "func_80051164",
         "func_80050F64", "func_80050F70", "func_8006DA18", "func_8002D198"}
# Texture loading, drawing setup and other menu branches are outside this seam.
natives = {"func_80007FE4", "func_800241C0", "func_800245AC", "func_8004C560",
           "func_80050E24", "func_80050FC4", "func_800563F0", "func_80057520",
           "func_80066D54", "func_80067220", "func_8006C9C4", "func_8006C9FC",
           "func_8006E18C", "func_8006E39C", "func_8006E4E4", "func_8006E740",
           "func_8006E99C", "func_8006EB8C", "func_8006F63C", "wdc_results_normal_text"}
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
if calls - roots - natives:
    raise SystemExit(f"Unexpected callees: {calls - roots - natives}")
if "wdc_results_normal_text" not in calls:
    raise SystemExit("Generated results drawer does not call wdc_results_normal_text")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(roots | calls)) + "\n" +
    "\n".join(functions[name] for name in sorted(roots)), encoding="utf-8")
