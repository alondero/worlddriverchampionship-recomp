"""Compare copied optimized and original RT64 hashers on synthetic TMEM."""
import argparse
import os
from pathlib import Path
import re
import subprocess
from fast_hash import make_fast_hasher


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", required=True, type=Path)
    args = parser.parse_args()
    cache = (args.reference_build / "CMakeCache.txt").read_text()
    source = Path(re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache, re.M)[1])
    compiler = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.*)$", cache, re.M)[1]
    rt64 = source / "lib/rt64/src"
    output = Path(__file__).resolve().parents[2] / "build/render-split-prototype"
    output.mkdir(parents=True, exist_ok=True)
    original = (rt64 / "common/rt64_tmem_hasher.h").read_text()
    (output / "original_tmem_hasher.h").write_text(original)
    (output / "fast_tmem_hasher.h").write_text(make_fast_hasher(original))
    fixture = output / "fast_hash_test.cpp"
    fixture.write_text(r'''#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#define XXH_INLINE_ALL
#include "xxHash/xxh3.h"
constexpr uint8_t G_IM_SIZ_4b=0, G_IM_SIZ_16b=2, G_IM_SIZ_32b=3, G_IM_FMT_RGBA=0;
struct LoadTile { uint16_t line, tmem; uint8_t siz, fmt, palette; };
#include "original_tmem_hasher.h"
#include "fast_tmem_hasher.h"
int main() {
    std::array<uint8_t,4096> tmem;
    uint32_t random=1234567;
    auto next=[&]() { random^=random<<13; random^=random>>17; random^=random<<5; return random; };
    for (unsigned trial=0; trial<20000; ++trial) {
        for (auto& b:tmem) b = trial%4==0 ? 0 : trial%4==1 ? uint8_t(next()%4) : uint8_t(next());
        LoadTile tile{uint16_t(next()%17), uint16_t(next()%512), uint8_t(next()%4), uint8_t(next()%5), uint8_t(next()%16)};
        const uint16_t width=1+next()%64, height=1+next()%32;
        const uint32_t tlut=next()%3, version=1+next()%5;
        auto a=RT64::TMEMHasher::hash(tmem.data(),tile,width,height,tlut,version);
        auto b=RT64::PrototypeFastTMEMHasher::hash(tmem.data(),tile,width,height,tlut,version);
        if (a!=b) { std::printf("Mismatch at trial %u\n", trial); return 1; }
    }
    // Exercise every contiguous CI8 range, including full sets, bit 63 and
    // runs crossing 64-bit boundaries. Palette values remain non-uniform.
    for (unsigned first=0; first<256; ++first) {
        for (unsigned count=1; count<=256-first; ++count) {
            for (auto& b:tmem) b=uint8_t(next());
            for (unsigned i=0;i<2048;++i) tmem[i]=uint8_t(first+i%count);
            LoadTile tile{8,0,1,2,0};
            auto a=RT64::TMEMHasher::hash(tmem.data(),tile,64,32,2,5);
            auto b=RT64::PrototypeFastTMEMHasher::hash(tmem.data(),tile,64,32,2,5);
            if(a!=b) { std::printf("Range mismatch %u/%u\n",first,count); return 1; }
        }
    }
    puts("PASS: 20000 original/optimized hash comparisons across formats, palettes, strides and versions");
    puts("PASS: all 32896 contiguous CI8 palette ranges match original hashes");
}
''')
    exe=output / "fast_hash_test.exe"
    subprocess.run([compiler,"-std=c++20","-O2","-Wall","-Wextra","-Werror","-I",str(rt64 / "contrib"),str(fixture),"-o",str(exe)],check=True)
    env=dict(os.environ)
    env["PATH"]=str(Path(compiler).parent)+os.pathsep+env.get("PATH", "")
    subprocess.run([str(exe)],env=env,check=True)


if __name__ == "__main__":
    main()
