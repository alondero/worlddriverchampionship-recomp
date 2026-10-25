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
pending = ["func_8004D450", "func_8004D79C", "func_8004D9B0", "func_8004D854", "func_8004D968", "func_80001D70",
           "wdc_pfs_init", "wdc_pfs_allocate_file", "wdc_pfs_find_file",
           "wdc_pfs_read_write_file", "wdc_pfs_num_files", "func_800742D0"]
# Control the single-threaded SDK's SI queues/count.
# File initialization, allocation, note I/O and effect timers remain translated.
natives = {"wdc_motor_init", "wdc_motor_start", "wdc_motor_stop",
           "wdc_rumble_tick", "wdc_pak_read", "wdc_pak_write", "wdc_pak_status",
           "osCreateMesgQueue_recomp", "osRecvMesg_recomp", "osSendMesg_recomp", "osGetCount_recomp"}
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
if "wdc_motor_init(rdram, ctx);" not in functions["func_8004D854"]:
    raise SystemExit("Rumble test requires the live status-transition initialization path")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
