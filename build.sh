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
if [[ "$CLEAN" == 1 && -f build/build.ninja ]]; then cmake --build build --target clean; fi

echo "[submodules]"
git submodule update --init --recursive
echo "[dependency patches]"
apply_patch_once() {
    local submodule="$1" patch="$ROOT/$2"
    if git -C "$submodule" apply --ignore-whitespace --reverse --check "$patch" 2>/dev/null; then
        return
    fi
    git -C "$submodule" apply --ignore-whitespace --check "$patch" || die "patch conflicts with local edits: $patch"
    git -C "$submodule" apply --ignore-whitespace "$patch"
}
apply_patch_once lib/N64ModernRuntime patches/0006-runtime-lle-interrupt-ownership.patch
apply_patch_once lib/N64ModernRuntime patches/0013-runtime-rsp-unsigned-packed-loads.patch
apply_patch_once lib/rt64 patches/0014-rt64-rdp-screen-clipping.patch
apply_patch_once lib/rt64 patches/0015-rt64-raw-triangle-scissor.patch
apply_patch_once lib/N64ModernRuntime patches/0008-runtime-controller-pak-kind.patch
apply_patch_once lib/N64ModernRuntime/N64Recomp patches/0007-rsprecomp-boss-support.patch
apply_patch_once lib/N64ModernRuntime patches/0009-runtime-host-config-storage.patch
apply_patch_once lib/RecompFrontend patches/0010-frontend-enum-wrap.patch
apply_patch_once lib/RecompFrontend patches/0011-frontend-runtime-compat.patch
apply_patch_once lib/RecompFrontend patches/0012-frontend-host-services.patch
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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWDC_HEADLESS=OFF -DWDC_ENABLE_FRONTEND=ON
echo "[host tools]"
cmake --build build --target N64RecompCLI RSPRecomp -j 8
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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWDC_HEADLESS=OFF -DWDC_ENABLE_FRONTEND=ON
echo "[build port]"
cmake --build build --target worlddriverchampionship_modern -j 8
echo "Build complete: build/worlddriverchampionship_modern"
