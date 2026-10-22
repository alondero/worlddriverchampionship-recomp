"""Extract ROM car/track visibility and detail decisions for bounded fixtures."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
roots = {"func_800218F0", "func_8002343C", "func_800230D4",
         "func_8001A74C", "func_8001A0C8", "func_8001A090"}
natives = {
    "func_80001D4C", "func_8000F8D8", "func_800153F4", "func_8001A040",
    "func_8001A0C0", "func_8001A0C8", "func_8001A79C", "func_8001A97C",
    "func_8001F7F8", "func_8001FC64", "func_80023BC0", "func_80023CFC",
    "func_80023F2C", "func_8002CF30", "func_8002D1FC", "func_8002D2B0",
    "func_8002D314", "func_8002D4F4", "func_8003062C",
    "wdc_car_detail", "wdc_car_distance", "wdc_track_detail", "wdc_track_visibility",
    "func_8001A8E8", "func_8001FBB0", "func_80022D84", "func_80022E34",
    "func_80071580", "func_8001A9BC", "func_8002CFB4",
}
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
    if "LOOKUP_FUNC" in body:
        raise SystemExit("Unexpected indirect visibility-test call")
if calls - natives - roots:
    raise SystemExit(f"Unexpected visibility-test boundary: {calls - natives - roots}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {n}(uint8_t*, recomp_context*);" for n in sorted(calls | roots)) +
    "\n" + "\n".join(functions[n] for n in sorted(roots)), encoding="utf-8")
