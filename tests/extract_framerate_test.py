"""Extract the real swap/callback/wheel animation with reviewed hooks.

OS queues and VI delivery are controlled by the test. Wheel geometry helpers
are outside the angle-advance boundary. No instruction or hook is rewritten.
"""
import re
import sys
from pathlib import Path

source, destination = map(Path, sys.argv[1:3])
roots = {"func_800013DC", "func_8000C0D0", "func_8000C164", "func_80017164",
         "func_80017DEC", "func_800177D0", "func_800178C4", "func_80017B14", "func_8001A4C4"}
natives = {
    "func_8002AC60", "func_80000678", "func_800216D0", "func_80019E28",
    "func_80001304", "func_8002AE50", "func_8006F63C", "func_8006F1E0",
    "osViBlack_recomp", "osSendMesg_recomp", "osRecvMesg_recomp",
    "osViSetMode_recomp", "osViSetSpecialFeatures_recomp",
    "osViSwapBuffer_recomp", "osViGetCurrentFramebuffer_recomp",
    "func_80019D98", "func_80013B78", "func_8001A000", "func_80013998",
    "wdc_framerate_action_lifetime", "wdc_framerate_wheels",
    "wdc_framerate_camera",
}
functions = {}
hud_hook = False
for path in sorted(source.glob("*.c")):
    for match in re.finditer(r"RECOMP_FUNC void (\w+)\(.*?(?=RECOMP_FUNC void |\Z)",
                             path.read_text(encoding="utf-8"), re.S):
        if match[1] in roots:
            functions[match[1]] = match[0]
        if match[1] == "func_8002E13C":
            hud_hook = "wdc_framerate_hud_age(rdram, ctx)" in match[0]
if roots - functions.keys():
    raise SystemExit(f"Missing translated bodies: {roots - functions.keys()}")
if not hud_hook:
    raise SystemExit("Missing generated HUD sample-age hook")
for hook in ("wdc_framerate_preswap()", "wdc_framerate_action_lifetime(rdram, ctx)",
             "wdc_framerate_wheels(rdram, ctx)", "wdc_framerate_camera(rdram, ctx)"):
    if not any(hook in body for body in functions.values()):
        raise SystemExit(f"Missing generated frame-rate hook: {hook}")
calls = set()
for body in functions.values():
    calls.update(re.findall(r"\b(\w+)\(rdram, ctx\);", body))
if calls - roots - natives:
    raise SystemExit(f"Unexpected callees: {calls - roots - natives}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(roots | calls)) + "\n" +
    "\n".join(functions[name] for name in sorted(roots)), encoding="utf-8")
