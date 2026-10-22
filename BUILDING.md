# Building from source

The tested host is Windows x64 with MinGW-w64 GCC 15.2, native CMake, Ninja,
Python 3.11+ (tomllib), Git, and a Direct3D 12 GPU. The SDL and shader compiler
DLLs come from the pinned RT64 dependencies. Internet access is needed for
initial recursive submodules and DirectX-Headers.

Place your own big-endian 16 MiB USA dump at the repository root as
`World Driver Championship (USA).z64`. The generator checks SHA-256:
`e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97`.
Other revisions and byte orders are rejected.

## Windows

```powershell
./build.ps1
./play.ps1
```

The build script prefers the native Python CMake installation and MinGW.
Override discovery with `WDC_PYTHON_SCRIPTS` and `WDC_MINGW_BIN` if necessary.
Close the running player before rebuilding. `-Clean` cleans build products
through CMake without resetting source files. `-RomPath` imports an alternate
local ROM path without overwriting a different existing dump.

Output: `build/worlddriverchampionship_modern.exe`, with SDL2, DXC,
FreeType, and MinGW runtime DLLs beside it, plus the overlay's `assets` directory.
`play.ps1` runs from the repository root
and accepts an optional `-RomPath`. Double-clicking the executable alone
requires a ROM argument or the expected working directory; use the launcher.

## Build stages

1. Initialise recursive pinned submodules without resetting local changes.
2. Apply [dependency patches](patches/README.md) idempotently; fail on conflicts.
3. Configure and build N64Recomp/RSPRecomp host tools.
4. Validate the ROM and generate symbols/configuration from measured inputs.
5. Translate CPU code and BOSS RSP code.
6. Reconfigure for newly generated sources and build the player.
7. Copy required Windows DLLs next to the executable.

Generated files are ignored: `RecompiledFuncs/`, `src/aspMain.cpp`, and
the CPU symbol/configuration TOMLs. Edit `rom-symbols.toml`,
`scripts/gen_syms_toml.py`, or `aspMain.us.toml`, never generated C/C++.

The script carries two Windows compatibility patches, three BOSS/runtime
patches, a Controller Pak device-kind patch, four settings-overlay patches,
and raw RDP screen-clipping, scissor-scaling and edge-reconstruction patches.
Pinned dependency edits are exported under `patches/`; dirty
submodules after applying those patches are expected.

## Checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest discover tests
python scripts/check_docs.py
python -B scripts/check_repo.py
git diff --check
```

For the ROM-free C++ checks used by pull-request CI, see
[host-test setup](docs/testing.md#rom-free-host-tests).

A separate `-DWDC_HEADLESS=ON` build provides a 15-second CPU/RSP diagnostic.
The desktop scripts explicitly enable `WDC_ENABLE_FRONTEND`; direct CMake
builds can set it to `OFF` to retain the graphics-shortcut-only player. Headless
builds omit the frontend automatically.
It deliberately exits with code 2 at its time limit: this is not a gameplay
pass. Setting `WDC_DIAGNOSTIC=1` applies the same time limit to the player.

## Other platforms

`build.sh` follows the same translation pipeline with SDL2/Vulkan, but Linux
has not been built or tested in this milestone. It requires the default ROM
filename at the repository root. Android remains scaffold-only; do not treat
this Windows milestone as Android validation.
