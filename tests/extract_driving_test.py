"""Extract the real input/drivetrain closure; only port hooks remain native."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
functions = {}
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        functions[match[1]] = match[0]
pending = ["func_80002F58", "func_80016DF8", "func_80017530"]
natives = {"wdc_driving_input_flags", "wdc_analog_throttle", "wdc_analog_brake"}
seen = set()
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name in natives:
        continue
    if name not in functions or "LOOKUP_FUNC" in functions[name]:
        raise SystemExit(f"Driving test requires a direct translated body for {name}")
    pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))
if not natives <= seen:
    raise SystemExit(f"Driving test requires generated hooks: {natives - seen}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
