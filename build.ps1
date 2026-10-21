[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$RomPath = "World Driver Championship (USA).z64"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path
Set-Location $repo

# Prefer the native Python CMake over MSYS2's CMake and put MinGW ahead of any
# `cc` shim. This is the same Windows toolchain guard used by the sibling
# projects; without it CMake can generate a Ninja graph containing MSYS paths
# that the native Ninja executable cannot read.
$pythonScripts = $env:WDC_PYTHON_SCRIPTS
if (-not $pythonScripts -and $env:LOCALAPPDATA) {
    $pythonRoot = Join-Path $env:LOCALAPPDATA "Programs\Python"
    if (Test-Path $pythonRoot) {
        $pythonScripts = Get-ChildItem -LiteralPath $pythonRoot -Directory -Filter "Python*" |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "Scripts" } |
            Where-Object { Test-Path (Join-Path $_ "cmake.exe") } |
            Select-Object -First 1
    }
}
$mingwBin = $env:WDC_MINGW_BIN
if (-not $mingwBin) {
    $compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
    if ($compiler) { $mingwBin = Split-Path $compiler.Source }
}
$prefix = @($pythonScripts, $mingwBin) | Where-Object { $_ -and (Test-Path $_) }
if ($prefix.Count -gt 0) { $env:PATH = ($prefix -join ";") + ";" + $env:PATH }

function Require-Command([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Missing required tool '$name'. See BUILDING.md."
    }
}
function Invoke-Step([string]$name, [scriptblock]$body) {
    Write-Host "[$name]" -ForegroundColor Cyan
    # Native tools may write warnings to stderr; their exit status is decisive.
    $ErrorActionPreference = 'Continue'
    & $body
    if ($LASTEXITCODE -ne 0) { throw "Step '$name' failed with exit code $LASTEXITCODE" }
}

foreach ($tool in @("cmake.exe", "ninja.exe", "gcc.exe", "g++.exe", "git.exe", "python.exe")) { Require-Command $tool }
$cmake = (Get-Command cmake.exe).Source
$python = (Get-Command python.exe).Source
if ($pythonScripts -and (Test-Path (Join-Path (Split-Path $pythonScripts) 'python.exe'))) {
    $python = Join-Path (Split-Path $pythonScripts) 'python.exe'
}
function Test-Patch([string]$submodule, [string]$patch, [switch]$Reverse) {
    # Windows PowerShell converts redirected native stderr into error records.
    # A failed applicability probe is expected, not a terminating build error.
    $ErrorActionPreference = 'Continue'
    $options = @('apply', '--ignore-whitespace', '--check')
    if ($Reverse) { $options += '--reverse' }
    git -C $submodule @options $patch 2>$null
    return $LASTEXITCODE -eq 0
}
$rom = (Resolve-Path $RomPath -ErrorAction SilentlyContinue)
if (-not $rom) { throw "Missing ROM '$RomPath'. Supply your own USA dump at the repository root." }

$expectedRom = Join-Path $repo 'World Driver Championship (USA).z64'
if ($rom.Path -ne $expectedRom) {
    if (Test-Path -LiteralPath $expectedRom) {
        if ((Get-FileHash $expectedRom).Hash -ne (Get-FileHash $rom.Path).Hash) {
            throw 'A different ROM already exists at the expected path; it was not overwritten.'
        }
    } else { Copy-Item -LiteralPath $rom.Path -Destination $expectedRom }
}
if ($Clean -and (Test-Path build/build.ninja)) {
    Invoke-Step 'clean build products' { & $cmake --build build --target clean }
}

Invoke-Step "submodules" { git submodule update --init --recursive }
Invoke-Step "apply dependency patches (preserving local edits)" {
        $patches = @(
            @{ Sub = "lib/rt64"; Patch = "patches/0005-rt64-mingw-gcc-compat.patch" },
            @{ Sub = "lib/rt64/src/contrib/plume"; Patch = "patches/0004-plume-d3d12-mingw-com-abi-struct-return.patch" },
            @{ Sub = "lib/N64ModernRuntime"; Patch = "patches/0006-runtime-lle-interrupt-ownership.patch" },
            @{ Sub = "lib/N64ModernRuntime"; Patch = "patches/0008-runtime-controller-pak-kind.patch" },
            @{ Sub = "lib/N64ModernRuntime/N64Recomp"; Patch = "patches/0007-rsprecomp-boss-support.patch" },
            @{ Sub = "lib/N64ModernRuntime"; Patch = "patches/0009-runtime-host-config-storage.patch" },
            @{ Sub = "lib/RecompFrontend"; Patch = "patches/0010-frontend-enum-wrap.patch" },
            @{ Sub = "lib/RecompFrontend"; Patch = "patches/0011-frontend-runtime-compat.patch" },
            @{ Sub = "lib/RecompFrontend"; Patch = "patches/0012-frontend-host-services.patch" },
            @{ Sub = "lib/N64ModernRuntime"; Patch = "patches/0013-runtime-rsp-unsigned-packed-loads.patch" },
            @{ Sub = "lib/rt64"; Patch = "patches/0014-rt64-rdp-screen-clipping.patch" },
            @{ Sub = "lib/rt64"; Patch = "patches/0015-rt64-raw-triangle-scissor.patch" }
        )
        foreach ($item in $patches) {
            $patchPath = Join-Path $repo $item.Patch
            if (Test-Patch $item.Sub $patchPath -Reverse) { continue }
            if (Test-Patch $item.Sub $patchPath) {
                git -C $item.Sub apply --ignore-whitespace $patchPath
                if ($LASTEXITCODE -ne 0) { throw "Failed to apply $($item.Patch)" }
            } else {
                throw "Patch conflicts with local edits: $($item.Patch)"
            }
        }
}
Invoke-Step "configure (first pass)" {
    & $cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWDC_HEADLESS=OFF -DWDC_ENABLE_FRONTEND=ON `
        -DCMAKE_C_COMPILER=gcc.exe -DCMAKE_CXX_COMPILER=g++.exe `
        "-DPython3_EXECUTABLE=$python"
}
Invoke-Step "build host tools" {
    & $cmake --build build --target N64RecompCLI RSPRecomp -j 8
}
Invoke-Step "generate symbol/config inputs" {
    & $python -B scripts/gen_syms_toml.py
}

$n64 = Join-Path $repo "build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp.exe"
$rsp = Join-Path $repo "build/lib/N64ModernRuntime/librecomp/N64Recomp/RSPRecomp.exe"
if (-not (Test-Path $n64)) { throw "N64Recomp was not built at $n64" }
if (-not (Test-Path $rsp)) { throw "RSPRecomp was not built at $rsp" }
Invoke-Step "recompile CPU" { & $n64 worlddriverchampionship.us.toml }
if ((Get-Content aspMain.us.toml -Raw) -notmatch "TODO") {
    Invoke-Step "recompile RSP" { & $rsp aspMain.us.toml }
} else {
    Write-Warning "aspMain.us.toml is still a placeholder; skipping RSPRecomp."
}

Invoke-Step "configure (second pass)" {
    & $cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWDC_HEADLESS=OFF -DWDC_ENABLE_FRONTEND=ON `
        -DCMAKE_C_COMPILER=gcc.exe -DCMAKE_CXX_COMPILER=g++.exe `
        "-DPython3_EXECUTABLE=$python"
}
Invoke-Step "build port" {
    & $cmake --build build --target worlddriverchampionship_modern -j 8
}
Write-Host "Build complete. Run ./play.ps1" -ForegroundColor Green
