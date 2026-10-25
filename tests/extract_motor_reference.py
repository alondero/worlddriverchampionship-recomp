"""Translate the unpatched USA motor initializer for differential evidence.

Generate separate recompiler inputs/output under the build tree. Production
symbols, configuration and generated functions are read-only. Only the original
initializer is emitted; its device probes are supplied by the ROM fixture.
"""
import hashlib
import re
import subprocess
import sys
import tomllib
from pathlib import Path

root, recompiler, destination = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
analysis = tomllib.loads((root / "rom-analysis.toml").read_text(encoding="utf-8"))["rom"]
rom = root / analysis["filename"]
if hashlib.sha256(rom.read_bytes()).hexdigest() != analysis["sha256"]:
    raise SystemExit("Motor reference requires the reviewed USA ROM")
symbols = (root / "worlddriverchampionship.syms.toml").read_text(encoding="utf-8")
renames = {"wdc_motor_init": "wdc_original_motor_init",
           "wdc_pak_read": "wdc_reference_probe_read",
           "wdc_pak_write": "wdc_reference_probe_write",
           "func_80073A70": "wdc_reference_pack_motor"}
for old, new in renames.items():
    needle = f'name = "{old}"'
    if symbols.count(needle) != 1:
        raise SystemExit(f"Motor reference requires exactly one symbol {old}")
    symbols = symbols.replace(needle, f'name = "{new}"')
# Prefix unrelated names so N64Recomp's SDK-name aliases cannot rewrite the
# ignore list. The entrypoint is emitted but never linked into the fixture.
keep = {*renames.values(), "recomp_entrypoint"}
symbols = re.sub(r'name = "(\w+)"', lambda match:
    f'name = "{match[1] if match[1] in keep else "wdc_reference_unused_" + match[1]}"', symbols)
names = re.findall(r'name = "(\w+)"', symbols)
ignored = sorted(name for name in names if name not in {"wdc_original_motor_init", "recomp_entrypoint"})
work = destination.parent / "motor-reference"
work.mkdir(exist_ok=True)
symbol_path, config_path = work / "symbols.toml", work / "config.toml"
symbol_path.write_text(symbols, encoding="utf-8")
config_path.write_text(
    "[input]\nentrypoint = 0x80000400\n" +
    f'output_func_path = "{(work / "functions").as_posix()}"\n' +
    f'symbols_file_path = "{symbol_path.as_posix()}"\n' +
    f'rom_file_path = "{rom.as_posix()}"\n' +
    "[patches]\nstubs = []\nignored = [" +
    ", ".join(f'"{name}"' for name in ignored) + "]\n", encoding="utf-8")
subprocess.run([str(recompiler), str(config_path)], cwd=work, check=True)
bodies = [match[0] for path in (work / "functions").glob("*.c")
          for match in re.finditer(r"RECOMP_FUNC void wdc_original_motor_init\(.*?(?=RECOMP_FUNC void |\Z)",
                                   path.read_text(encoding="utf-8"), re.S)]
if len(bodies) != 1 or "LOOKUP_FUNC" in bodies[0]:
    raise SystemExit("Motor reference requires one direct translated initializer")
calls = set(re.findall(r"\b(\w+)\(rdram, ctx\);", bodies[0]))
if calls != {"wdc_reference_probe_read", "wdc_reference_probe_write", "wdc_reference_pack_motor"}:
    raise SystemExit(f"Unexpected motor reference call closure: {sorted(calls)}")
destination.write_text('#include "recomp.h"\n' +
    "\n".join(f"void {name}(uint8_t*, recomp_context*);" for name in sorted(calls)) +
    "\n" + bodies[0], encoding="utf-8")
