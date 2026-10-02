param([string]$Output = 'build-player/window.png', [int]$ProcessId = 0)
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
if (!$game) { throw 'Game window not found' }
$rect = New-Object WdcWindowCapture+Rect
[void][WdcWindowCapture]::GetWindowRect($game.MainWindowHandle, [ref]$rect)
$bitmap = New-Object System.Drawing.Bitmap(($rect.Right-$rect.Left), ($rect.Bottom-$rect.Top))
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try { [void][WdcWindowCapture]::PrintWindow($game.MainWindowHandle, $dc, 2) }
finally { $graphics.ReleaseHdc($dc) }
$bitmap.Save((Join-Path $PWD $Output))
$graphics.Dispose()
$bitmap.Dispose()
Write-Output $Output
