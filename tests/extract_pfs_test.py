"""Extract the real translated PFS call closure into the ignored build tree.

Missing bodies and unexpected native calls fail instead of replacing filesystem
behavior. The test replaces SI locking/queues and the timer count in its single
guest thread.
"""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
functions = {}
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        functions[match[1]] = match[0]
pending = ["wdc_pfs_init", "wdc_pfs_allocate_file", "wdc_pfs_find_file",
           "wdc_pfs_read_write_file", "wdc_pfs_free_blocks", "wdc_pfs_num_files",
           "func_800720B0", "func_800734D0", "func_800742D0"]
natives = {"wdc_pak_read", "wdc_pak_write", "wdc_pak_status",
           "osCreateMesgQueue_recomp", "osRecvMesg_recomp", "osSendMesg_recomp",
           "osGetCount_recomp"}
seen = set()
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name in natives:
        continue
    if name not in functions:
        raise SystemExit(f"PFS test requires a translated body for {name}")
    body = functions[name]
    if "LOOKUP_FUNC" in body:
        raise SystemExit(f"Unexpected indirect call in PFS function {name}")
    pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
