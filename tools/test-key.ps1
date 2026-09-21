param([int]$Key = 88, [int]$Milliseconds = 150)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class WdcTestKey {
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
}
'@
$game = Get-Process worlddriverchampionship_modern | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
if (!$game) { throw 'Game window not found' }
$scan = [WdcTestKey]::MapVirtualKey($Key, 0)
$flags = 1 -bor ($scan -shl 16)
if ($Key -in 37,38,39,40) { $flags = $flags -bor 0x01000000 }
[void][WdcTestKey]::PostMessage($game.MainWindowHandle, 0x100, [IntPtr]$Key, [IntPtr]$flags)
Start-Sleep -Milliseconds $Milliseconds
[void][WdcTestKey]::PostMessage($game.MainWindowHandle, 0x101, [IntPtr]$Key, [IntPtr]($flags -bor 0xC0000000L))
