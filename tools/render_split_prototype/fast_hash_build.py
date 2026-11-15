"""Optional copied RT64 palette hash optimization, never edits the reference."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
from fast_hash import make_fast_hasher


def build_fast_hash(reference, output, compiler, cache_palette=False):
    entries = json.loads((reference / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/hle/rt64_rdp_tmem.cpp"))
    original = Path(entry["file"]).read_bytes()
    source = original.decode().replace("\r\n", "\n")
    needle = "        uint64_t hash = TMEMHasher::hash(TMEM, loadTile, width, height, tlut, TMEMHasher::CurrentHashVersion);"
    if source.count(needle) != 1:
        raise RuntimeError("RT64 texture hash seam changed")
    header_path = Path(entry["file"]).parent.parent / "common/rt64_tmem_hasher.h"
    header = header_path.read_bytes()
    (output / "fast_tmem_hasher.h").write_text(make_fast_hasher(header.decode().replace("\r\n", "\n"), cache_palette))
    if source.count('#include "common/rt64_tmem_hasher.h"') != 1:
        raise RuntimeError("RT64 hasher include seam changed")
    source = source.replace('#include "common/rt64_tmem_hasher.h"', '#include "common/rt64_tmem_hasher.h"\n#include "fast_tmem_hasher.h"')
    source = source.replace(needle, '''        uint64_t hash = PrototypeFastTMEMHasher::hash(TMEM, loadTile, width, height, tlut, TMEMHasher::CurrentHashVersion);
        // The launch setting is immutable; getenv per texture dominates timing.
        static const bool verify = std::getenv("WDC_RENDER_PROTOTYPE_VERIFY_TEXTURE_HASH") != nullptr;
        if (verify && hash != TMEMHasher::hash(TMEM, loadTile, width, height, tlut, TMEMHasher::CurrentHashVersion)) std::_Exit(19);
        prototype_texture_hash_result(verify);''')
    if cache_palette:
        source = source.replace('        uint64_t hash = PrototypeFastTMEMHasher::hash',
            '        const auto lookups = PrototypeFastTMEMHasher::paletteLookups, hits = PrototypeFastTMEMHasher::paletteHits;\n        uint64_t hash = PrototypeFastTMEMHasher::hash')
        source = source.replace('prototype_texture_hash_result(verify);',
            'prototype_texture_hash_result(verify, PrototypeFastTMEMHasher::paletteLookups - lookups, PrototypeFastTMEMHasher::paletteHits - hits);')
    else:
        source = source.replace('prototype_texture_hash_result(verify);', 'prototype_texture_hash_result(verify, 0, 0);')
    source = '#include <cstdlib>\n#include <cstdint>\nextern "C" void prototype_texture_hash_result(bool, uint64_t, uint64_t);\n' + source
    copied = output / "fast_rt64_rdp_tmem.cpp"
    copied.write_text(source)
    obj = output / "fast_rt64_rdp_tmem.o"
    command = shlex.split(entry["command"])
    command[0] = compiler
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(copied)
    command.extend(["-I", str(Path(entry["file"]).parent), "-I", str(Path(__file__).resolve().parent)])
    result = subprocess.run(command, cwd=entry["directory"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "fast-hash-build.log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError("Optimized hasher compilation failed; inspect fast-hash-build.log")
    (output / "fast-hash-source.json").write_text(json.dumps({
        "cache_palette": cache_palette,
        "source": entry["file"], "source_sha256": hashlib.sha256(original).hexdigest(),
        "copied_sha256": hashlib.sha256(copied.read_bytes()).hexdigest(),
        "header": str(header_path), "header_sha256": hashlib.sha256(header).hexdigest(),
        "fast_header_sha256": hashlib.sha256((output / "fast_tmem_hasher.h").read_bytes()).hexdigest(),
        "compile_command": command}, indent=2))
    return str(obj)
