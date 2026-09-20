#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help) sed -n '1,35p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $arg" >&2; exit 2 ;;
    esac
done

die() { echo "ERROR: $*" >&2; exit 1; }
for tool in cmake ninja git python3; do
    command -v "$tool" >/dev/null || die "missing tool: $tool"
done

ROM="${ROM_FILENAME:-World Driver Championship (USA).z64}"
[[ -f "$ROM" ]] || die "missing ROM: $ROM (supply your own USA dump)"
if [[ "$CLEAN" == 1 && -d build ]]; then rm -rf build; fi

echo "[submodules]"
git submodule update --init --recursive
echo "[dependency worktrees]"
for submodule in lib/N64ModernRuntime lib/rt64 lib/rt64/src/contrib/plume lib/RecompFrontend; do
    git -C "$submodule" checkout -- .
done
echo "[dependency patches]"
windows_host=0
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) windows_host=1 ;;
esac
if [[ "${WDC_APPLY_WINDOWS_PATCHES:-0}" == 1 || "$windows_host" == 1 ]]; then
    patches=(
        "lib/rt64:patches/0005-rt64-mingw-gcc-compat.patch"
        "lib/rt64/src/contrib/plume:patches/0004-plume-d3d12-mingw-com-abi-struct-return.patch"
    )
    for entry in "${patches[@]}"; do
        submodule="${entry%%:*}"
        patch="${entry#*:}"
        if git -C "$submodule" apply --ignore-whitespace --check "$ROOT/$patch"; then
            git -C "$submodule" apply --ignore-whitespace "$ROOT/$patch"
        elif ! git -C "$submodule" apply --ignore-whitespace --reverse --check "$ROOT/$patch"; then
            die "dependency patch does not apply cleanly: $patch"
        fi
    done
fi
echo "[configure: first pass]"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
echo "[host tools]"
cmake --build build --target N64RecompCLI RSPRecomp
echo "[symbol/config generation]"
python3 -B scripts/gen_syms_toml.py

N64="build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp"
RSP="build/lib/N64ModernRuntime/librecomp/N64Recomp/RSPRecomp"
[[ -x "$N64" ]] || die "N64Recomp was not built at $N64"
[[ -x "$RSP" ]] || die "RSPRecomp was not built at $RSP"
echo "[recompile CPU]"
"$N64" worlddriverchampionship.us.toml
if ! grep -q TODO aspMain.us.toml; then
    echo "[recompile RSP]"
    "$RSP" aspMain.us.toml
else
    echo "WARNING: aspMain.us.toml is still a placeholder; skipping RSPRecomp." >&2
fi

echo "[configure: second pass]"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
echo "[build port]"
cmake --build build --target worlddriverchampionship_modern
echo "Bootstrap build complete: build/worlddriverchampionship_modern"
