"""Extract the warped-exit menu chain, native warp constructor and selection setup."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
functions = {m[1]: m[0] for path in sorted(source.glob("*.c"))
             for m in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                                  path.read_text(encoding="utf-8"), re.S)}
chain = ["func_8004E950", "func_800525F8", "func_80053448", "func_80057FD4",
         "func_80069E30", "func_8006A64C", "func_800565E0"]
control = {"func_80058650", "func_80069138", "func_80069144", "func_80056BC0",
             "func_800691B0", "func_800563FC", "func_80066C7C"}
# The race constructor closure is real, as in the warp fixture. Rendering,
# presentation, input, SDK and preview boundaries are stubs in this focused seam.
pending = ["func_80068AA8", "func_8000A200", "func_8000A218"]
natives = {"wdc_warp_laps", "wdc_quick_race_profile", "wdc_cheat_car_available", "wdc_warp_menu", "func_8006F63C"}
seen = set()
while pending:
    name = pending.pop()
    if name in seen or name in natives:
        continue
    seen.add(name)
    if name not in functions or "LOOKUP_FUNC" in functions[name]:
        raise SystemExit(f"Warp exit test requires a direct translated body for {name}")
    pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))
actual = set(chain) | control | seen
missing = [name for name in chain + sorted(control) if name not in functions]
if missing:
    raise SystemExit(f"Missing translated bodies: {missing}")
for parent, child in zip(chain, chain[1:]):
    if not re.search(rf"\b{child}\(rdram, ctx\);", functions[parent]):
        raise SystemExit(f"Missing required chain edge {parent} -> {child}")
calls = set()
for name in actual:
    if "LOOKUP_FUNC" in functions[name]:
        raise SystemExit(f"Unresolved computed control flow in {name}")
    calls.update(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))
stubs = sorted(calls - actual - natives)
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(actual | calls)) + "\n" +
    "\n".join(functions[name] for name in sorted(actual - natives)) + "\n" +
    "\n".join(f"void {name}(uint8_t* rdram, recomp_context* ctx) {{ (void)rdram; (void)ctx; }}" for name in stubs) + "\n",
    encoding="utf-8")
