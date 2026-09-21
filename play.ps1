[CmdletBinding()]
param([string]$RomPath = 'World Driver Championship (USA).z64')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $player = Join-Path $PSScriptRoot 'build-player/worlddriverchampionship_modern.exe'
    if (!(Test-Path -LiteralPath $player)) { throw 'Run ./build.ps1 first.' }
    $rom = (Resolve-Path -LiteralPath $RomPath).Path
    Write-Host 'Enter: Start/pause; X: A/accelerate; Z: B/brake; arrows/WASD: steer; Escape: quit.'
    Write-Host 'No Controller Pak: select Play Anyway. Saving is not supported yet.'
    & $player $rom
    exit $LASTEXITCODE
} finally { Pop-Location }
