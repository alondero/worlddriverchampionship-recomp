"""Extract actual translated cheat accessors, requiring every generated hook."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
expected = {
    "func_800565E0": "wdc_cheat_car_available",
    "func_800565EC": "wdc_cheat_car_seen",
    "func_80056974": "wdc_cheat_team_available",
    "func_80056B90": "wdc_cheat_team_class",
    "func_80057000": "wdc_cheat_track_available",
    "func_80057420": "wdc_cheat_event_available",
    "func_8006663C": "wdc_cheat_restart_count",
    "func_80066648": "wdc_cheat_restart_spend",
    "func_8006969C": "wdc_cheat_race_select",
    "func_800696A8": "wdc_cheat_ending",
}
callers = {
    "func_80066558": ("func_80056974", 0x80066584),
    "func_800664F4": ("func_80057420", 0x80066520),
    "func_80056B18": ("func_800565EC", 0x80056B50),
}
roots = expected.keys() | callers.keys()
functions = {}
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        if match[1] in roots:
            functions[match[1]] = match[0]
if roots - functions.keys():
    raise SystemExit(f"Missing translated bodies: {roots - functions.keys()}")
for name, body in functions.items():
    if "LOOKUP_FUNC" in body:
        raise SystemExit(f"Unexpected indirect call in {name}")
    if name in expected and expected[name] not in body:
        raise SystemExit(f"Cheat test requires the generated {expected[name]} hook in {name}")
    calls = set(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
    if calls - roots - set(expected.values()):
        raise SystemExit(f"Unexpected accessor callees: {calls - roots - set(expected.values())}")
    if name in callers:
        callee, return_address = callers[name]
        marker = f"ctx->r31 = (int32_t)0x{return_address:08X}u;"
        call = f"{callee}(rdram, ctx);"
        if marker not in body or call not in body or body.index(marker) > body.index(call):
            raise SystemExit(f"Missing actual JAL link-register assignment in {name}")

# Hooks retain their original declarations in these generated bodies. Do not
# replace any guest load, branch, store or hook call with test implementations.
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(roots)) + "\n" +
    "\n".join(functions[name] for name in sorted(functions)), encoding="utf-8")
