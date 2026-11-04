[CmdletBinding()]
param(
    [string]$RomPath = 'World Driver Championship (USA).z64',
    [string]$Warp
)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
$previousWarp = $env:WDC_WARP
try {
    if ($PSBoundParameters.ContainsKey('Warp')) { $env:WDC_WARP = $Warp }
    $player = Join-Path $PSScriptRoot 'build/worlddriverchampionship_modern.exe'
    if (!(Test-Path -LiteralPath $player)) { throw 'Run ./build.ps1 first.' }
    $rom = (Resolve-Path -LiteralPath $RomPath).Path
    Write-Host 'Enter: Start/pause; X: A/accelerate; Z: B/brake; arrows/WASD: steer; Escape/F1: settings.'
    Write-Host 'Controller Pak saves are enabled on controller port 1.'
    & $player $rom
    exit $LASTEXITCODE
} finally { $env:WDC_WARP = $previousWarp; Pop-Location }
