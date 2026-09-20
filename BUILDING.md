# Building from source

This is a development checkout, not a release package. A source build needs
the repository, its recursive submodules, and your own legally dumped ROM.
The ROM is read during translation and at runtime; it is never committed.

## Supported development hosts

| Host | Toolchain | Renderer | Status |
| --- | --- | --- | --- |
| 64-bit Windows | MinGW-w64 GCC/G++, CMake, Ninja | RT64 / Direct3D 12 | Planned |
| 64-bit Linux | GCC/G++, CMake, Ninja | RT64 / Vulkan | Planned |
| ARM64 Android | Android NDK + Gradle | RT64 / Vulkan | Planned |

macOS is not a supported target until this port has explicit SDL window/input
and renderer coverage.

## Prerequisites

All desktop builds need Git with recursive submodules, CMake 3.20+, Python 3,
Ninja, and a C17/C++20 compiler. Linux additionally needs SDL2, Vulkan,
FreeType, X11, and dbus development packages. Windows needs MinGW-w64, CMake,
and Ninja; enable long paths when RT64's dependency tree requires it:

```powershell
git config --global core.longpaths true
```

## Bootstrap sequence

The supported scripts deliberately use two CMake passes:

1. validate tools and the ROM;
2. initialise and reset dependency submodules;
3. apply the patch series listed in `patches/README.md`;
4. configure and build N64Recomp, RSPRecomp, and host tools;
5. generate `RecompiledFuncs/` and `src/aspMain.cpp` from the ROM;
6. configure again so CMake discovers generated sources; and
7. build `worlddriverchampionship_modern`.

Run from the repository root:

```powershell
.\build.ps1
.\build.ps1 -Clean
```

```bash
./build.sh
./build.sh --clean
```

The default ROM filename is `World Driver Championship (USA).z64`. Override it
with `-RomPath` on Windows or `ROM_FILENAME` on Linux/CI.

## ROM analysis and generated inputs

Do not invent symbol boundaries or copy addresses from another N64 game. First
measure the actual dump and fill `rom-analysis.toml`; then:

```bash
python scripts/gen_syms_toml.py
```

The generator writes the ignored `worlddriverchampionship.syms.toml` and
`worlddriverchampionship.us.toml`. Review them, run N64Recomp, and investigate
every recompiler error by changing a checked-in input rather than generated C.
`aspMain.us.toml` is the hand-maintained RSPRecomp input and should be filled
only after the audio microcode is identified.

## Dependency patches

Patch files are the source of truth for local dependency changes. Start from
the pinned submodule commit, apply the documented series, make the smallest
change, add a focused test, and compare any reusable fix with upstream. If a
patch stops applying, stop and investigate dependency drift; do not leave a
dirty submodule as an undocumented dependency fork.

## Manual stages

When the full script is too broad for an investigation:

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target N64RecompCLI RSPRecomp
python scripts/gen_syms_toml.py
build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp worlddriverchampionship.us.toml
if ! grep -q TODO aspMain.us.toml; then
    build/lib/N64ModernRuntime/librecomp/N64Recomp/RSPRecomp aspMain.us.toml
fi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target worlddriverchampionship_modern
```

The final two translation commands are expected to remain unavailable until
ROM analysis has supplied valid inputs. That is an explicit bootstrap gate,
not a reason to fabricate placeholder generated files.
