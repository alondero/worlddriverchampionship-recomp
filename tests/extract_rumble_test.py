"""Extract WDC's real effect engine, retaining its timers and detection gates."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
functions = {}
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        functions[match[1]] = match[0]
pending = ["func_8004D450", "func_8004D79C", "func_8004D9B0", "func_8004DCA0"]
# Only the scheduler's gameplay predicate is controlled by the fixture.
natives = {"wdc_motor_init", "wdc_motor_start", "wdc_motor_stop",
           "wdc_rumble_tick", "func_80001D4C"}
seen = set()
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name in natives:
        continue
    if name not in functions:
        raise SystemExit(f"Rumble test requires a translated body for {name}")
    body = functions[name]
    if "LOOKUP_FUNC" in body:
        raise SystemExit(f"Unexpected indirect call in rumble function {name}")
    pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
if "wdc_rumble_tick(rdram, ctx);" not in functions["func_8004D450"]:
    raise SystemExit("Rumble test requires the generated heartbeat hook")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
