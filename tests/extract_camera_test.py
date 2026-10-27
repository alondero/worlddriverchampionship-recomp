"""Build real ROM camera and viewport closures with explicit native boundaries."""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
functions = {}
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        functions[match[1]] = match[0]
pending = ["func_80011D54", "func_8000FC18", "func_8000E148", "func_8002D99C",
           "func_800006E0", "func_800006AC", "func_8000125C"]
# The layout harness retains real viewport partition arithmetic and the scene
# rectangle writer, display initialization, descriptor lookup and mode selection.
# Framebuffer allocation, camera orientation and heap clearing are outside that
# seam. No projection/matrix calculation or descriptor construction is replaced.
natives = {"func_8002CF30", "func_8002D558", "wdc_align_hud_triangle",
           "wdc_widen_camera", "wdc_remove_overscan", "wdc_projection_distance",
           "func_8003C9A4", "func_8003C95C", "func_8000E020", "func_8000E18C",
           "func_8000E1BC", "func_8000F988", "func_80012024", "func_80012218",
           "func_800122F4", "func_8002A334"}
seen = set()
while pending:
    name = pending.pop()
    if name in seen:
        continue
    seen.add(name)
    if name in natives:
        continue
    if name not in functions or "LOOKUP_FUNC" in functions[name]:
        raise SystemExit(f"Camera test requires a direct translated body for {name}")
    pending.extend(re.findall(r"\b(\w+)\(rdram, ctx\);", functions[name]))
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(seen)) + "\n" +
    "\n".join(functions[name] for name in sorted(seen - natives)), encoding="utf-8")
