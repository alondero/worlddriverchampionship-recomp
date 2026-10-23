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
# The helper restores the paths a series owns to the pinned submodule state
# before applying it, so repeated builds stay idempotent even where a later
# patch rewrites the context an earlier patch matched. Every submodule gets
# exactly one ordered series, and the series match build.ps1.
apply_patch_series() {
    local submodule="$1"; shift
    if [[ $# -eq 0 ]]; then
        echo "no patches for $submodule on this host" >&2
        return 0
    fi
    local args=(-B "$ROOT/scripts/patch_series.py" --submodule "$submodule")
    local patch
    for patch in "$@"; do
        args+=(--patch "$patch")
    done
    python3 "${args[@]}"
}
windows_host=0
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) windows_host=1 ;;
esac
rt64_patches=(
    "$ROOT/patches/0014-rt64-rdp-screen-clipping.patch"
    "$ROOT/patches/0015-rt64-raw-triangle-scissor.patch"
    "$ROOT/patches/0016-rt64-rdp-edge-trapezoids.patch"
)
runtime_patches=(
    "$ROOT/patches/0006-runtime-lle-interrupt-ownership.patch"
    "$ROOT/patches/0008-runtime-controller-pak-kind.patch"
    "$ROOT/patches/0009-runtime-host-config-storage.patch"
    "$ROOT/patches/0013-runtime-rsp-unsigned-packed-loads.patch"
)
frontend_patches=(
    "$ROOT/patches/0010-frontend-enum-wrap.patch"
    "$ROOT/patches/0011-frontend-runtime-compat.patch"
    "$ROOT/patches/0012-frontend-host-services.patch"
    "$ROOT/patches/0017-frontend-host-input.patch"
)
windows_rt64_patches=()
windows_plume_patches=()
if [[ "${WDC_APPLY_WINDOWS_PATCHES:-0}" == 1 || "$windows_host" == 1 ]]; then
    # Windows-only patches join the same series as the shared ones, so a
    # submodule is never patched by two separate runs.
    windows_rt64_patches=("$ROOT/patches/0005-rt64-mingw-gcc-compat.patch")
    windows_plume_patches=("$ROOT/patches/0004-plume-d3d12-mingw-com-abi-struct-return.patch")
fi
apply_patch_series lib/rt64 "${windows_rt64_patches[@]}" "${rt64_patches[@]}"
apply_patch_series lib/N64ModernRuntime "${runtime_patches[@]}"
apply_patch_series lib/N64ModernRuntime/N64Recomp \
    "$ROOT/patches/0007-rsprecomp-boss-support.patch"
apply_patch_series lib/RecompFrontend "${frontend_patches[@]}"
if [[ ${#windows_plume_patches[@]} -gt 0 ]]; then
    apply_patch_series lib/rt64/src/contrib/plume "${windows_plume_patches[@]}"
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
