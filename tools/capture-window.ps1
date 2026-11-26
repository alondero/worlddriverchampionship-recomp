param([string]$Output = 'build/window.png', [int]$ProcessId = 0)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class WdcWindowCapture {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
}
'@
$game = if ($ProcessId) { Get-Process -Id $ProcessId } else {
    Get-Process worlddriverchampionship_modern | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
}
if (!$game -or $game.MainWindowHandle -eq 0) { throw 'Game window not found' }
$rect = New-Object WdcWindowCapture+Rect
if (![WdcWindowCapture]::GetWindowRect($game.MainWindowHandle, [ref]$rect)) {
    throw 'Could not read the game window bounds'
}
$bitmap = New-Object System.Drawing.Bitmap(($rect.Right-$rect.Left), ($rect.Bottom-$rect.Top))
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try {
    try {
        if (![WdcWindowCapture]::PrintWindow($game.MainWindowHandle, $dc, 2)) {
            throw 'Could not capture the game window'
        }
    } finally { $graphics.ReleaseHdc($dc) }
    $bitmap.Save($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output))
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}
Write-Output $Output
