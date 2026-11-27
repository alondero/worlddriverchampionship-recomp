"""Extract ROM car, track and scenery visibility decisions for bounded fixtures."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
roots = {"func_800108B4", "func_800218F0", "func_8002343C", "func_800230D4",
         "func_8001A74C", "func_8001A0C8", "func_8001A090",
         "func_800224F0", "func_8001A22C",
         "func_80010DA0", "func_8002CE10", "func_8000DF70", "func_8002703C",
         "func_80025378", "func_8003C9DC"}
natives = {
    "func_80001D4C", "func_8000F8D8", "func_800153F4", "func_8001A040",
    "func_8001A0C0", "func_8001A0C8", "func_8001A79C", "func_8001A97C",
    "func_8001F7F8", "func_8001FC64", "func_80023BC0", "func_80023CFC",
    "func_80023F2C", "func_8002CF30", "func_8002D1FC", "func_8002D2B0",
    "func_8002D314", "func_8002D4F4", "func_8003062C",
    "func_80019F60", "func_80022660", "func_8002D378", "func_80026208", "func_8006F63C",
    "wdc_car_detail", "wdc_car_distance", "wdc_scenery_distance", "wdc_track_detail", "wdc_track_visibility",
    "wdc_menu_car_visibility", "wdc_reserve_frame_arena", "wdc_depth_sort_range", "wdc_reserve_depth_sort_tails",
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
for name, body in functions.items():
    calls.update(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
    # The translated distance helper includes an alternate entry after the
    # squared-distance return. The test aborts if any indirect call is reached.
    if "LOOKUP_FUNC" in body and name != "func_8001A22C":
        raise SystemExit("Unexpected indirect visibility-test call")
if calls - natives - roots:
    raise SystemExit(f"Unexpected visibility-test boundary: {calls - natives - roots}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {n}(uint8_t*, recomp_context*);" for n in sorted(calls | roots)) +
    "\n" + "\n".join(functions[n] for n in sorted(roots)), encoding="utf-8")
