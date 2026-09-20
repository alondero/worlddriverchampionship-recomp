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
if (-not $mingwBin) { $mingwBin = "C:\ProgramData\mingw64\mingw64\bin" }
$prefix = @($pythonScripts, $mingwBin) | Where-Object { $_ -and (Test-Path $_) }
if ($prefix.Count -gt 0) { $env:PATH = ($prefix -join ";") + ";" + $env:PATH }

function Require-Command([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "Missing required tool '$name'. See BUILDING.md."
    }
}
function Invoke-Step([string]$name, [scriptblock]$body) {
    Write-Host "[$name]" -ForegroundColor Cyan
    & $body
    if ($LASTEXITCODE -ne 0) { throw "Step '$name' failed with exit code $LASTEXITCODE" }
}

foreach ($tool in @("cmake.exe", "ninja.exe", "gcc.exe", "g++.exe", "git.exe", "python.exe")) { Require-Command $tool }
$cmake = (Get-Command cmake.exe).Source
$python = (Get-Command python.exe).Source
$rom = (Resolve-Path $RomPath -ErrorAction SilentlyContinue)
if (-not $rom) { throw "Missing ROM '$RomPath'. Supply your own USA dump at the repository root." }

if ($Clean -and (Test-Path build)) { Remove-Item -Recurse -Force -LiteralPath build }

Invoke-Step "submodules" { git submodule update --init --recursive }
Invoke-Step "reset dependency worktrees" {
    foreach ($submodule in @("lib/N64ModernRuntime", "lib/rt64", "lib/rt64/src/contrib/plume", "lib/RecompFrontend")) {
        git -C $submodule checkout -- .
    }
}
if ($IsWindows -or $env:OS -eq "Windows_NT") {
    Invoke-Step "apply Windows dependency patches" {
        $patches = @(
            @{ Sub = "lib/rt64"; Patch = "patches/0005-rt64-mingw-gcc-compat.patch" },
            @{ Sub = "lib/rt64/src/contrib/plume"; Patch = "patches/0004-plume-d3d12-mingw-com-abi-struct-return.patch" }
        )
        foreach ($item in $patches) {
            $patchPath = Join-Path $repo $item.Patch
            git -C $item.Sub apply --ignore-whitespace --check $patchPath 2>$null
            if ($LASTEXITCODE -eq 0) {
                git -C $item.Sub apply --ignore-whitespace $patchPath
            } else {
                git -C $item.Sub apply --ignore-whitespace --reverse --check $patchPath 2>$null
                if ($LASTEXITCODE -ne 0) { throw "Patch does not apply cleanly: $($item.Patch)" }
            }
        }
    }
}
Invoke-Step "configure (first pass)" {
    & $cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=gcc.exe -DCMAKE_CXX_COMPILER=g++.exe `
        "-DPython3_EXECUTABLE=$python"
}
Invoke-Step "build host tools" {
    & $cmake --build build --target N64RecompCLI RSPRecomp
}
Invoke-Step "generate symbol/config inputs" {
    python -B scripts/gen_syms_toml.py
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
    & $cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=gcc.exe -DCMAKE_CXX_COMPILER=g++.exe `
        "-DPython3_EXECUTABLE=$python"
}
Invoke-Step "build port" {
    & $cmake --build build --target worlddriverchampionship_modern
}
Write-Host "Bootstrap build complete: build/worlddriverchampionship_modern.exe" -ForegroundColor Green
