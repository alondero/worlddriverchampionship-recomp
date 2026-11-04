"""Extract native Quick Race setup and fade setters, requiring the lap hook."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
functions = {m[1]: m[0] for path in sorted(source.glob("*.c"))
             for m in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                                  path.read_text(encoding="utf-8"), re.S)}
pending = ["func_80068AA8", "func_8000A200", "func_8000A218"]
natives = {"wdc_warp_laps", "func_8006F63C"}  # Only guest debug printing is stubbed.
seen = set()
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name in natives:
        continue
    if name not in functions or "LOOKUP_FUNC" in functions[name]:
        raise SystemExit(f"Warp test requires a direct translated body for {name}")
    pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))
if not natives <= seen:
    raise SystemExit(f"Warp test requires generated hooks: {natives - seen}")
# Check the live transition hooks too: the fixture calls these boundaries
# directly, so missing production wiring must fail extraction.
for name, hook in {"func_8004E950": "wdc_warp_menu", "func_8000C560": "wdc_warp_race",
                   "func_800369E0": "wdc_warp_load"}.items():
    if hook not in functions.get(name, ""):
        raise SystemExit(f"Missing {hook} in {name}")
boot = functions["func_8003BC50"]
if boot.count("wdc_warp_boot_pending()") != 3:
    raise SystemExit("Missing boot warp hooks")
# Execute the real boot control flow with presentation, allocation, input and
# SDK scan boundaries replaced. Count scan/dialog calls: pending launch must
# retain the former and skip the latter, even with B held. The Pak filesystem
# itself has separate ROM-backed coverage and is not validated by this fixture.
boot_calls = set(re.findall(r"\b(\w+)\(rdram, ctx\);", boot))
boot_stubs = []
for name in sorted(boot_calls - seen):
    counter = "++wdc_boot_scan_calls;" if name == "func_8003B984" else (
        "++wdc_boot_dialog_calls;" if name in {"func_8003897C", "func_8003AB80", "func_800387F8",
            "func_80039608", "func_80038454", "func_8003B6A0", "func_80003658"} else "")
    boot_stubs.append(f"void {name}(uint8_t* rdram, recomp_context* ctx) {{ {counter} ctx->r2 = 0; }}")
destination.write_text('#include "recomp.h"\n' +
    "unsigned wdc_boot_scan_calls = 0, wdc_boot_dialog_calls = 0;\n" +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen | boot_calls)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)) + "\n" +
    "\n".join(boot_stubs) + "\n" + boot, encoding="utf-8")
