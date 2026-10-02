# Configuration

Graphics settings are stored in `build/user-data/graphics.json`. The SDL main
thread loads and saves settings; the graphics thread takes a mutex-protected
copy at display-list/VI boundaries. SDL window operations stay on the main
thread. Saving uses a temporary file and replacement after a successful write.
Malformed files are preserved on load and defaults are used. An explicit
settings change replaces the file; a save failure is reported without losing
the live change.

Use the Windows **Graphics** menu or the keyboard shortcuts below. F1 shows
help; the window title shows requested settings. These controls also work in
fullscreen. Most changes apply live; color precision requires a restart.

| Shortcut | Setting | Choices |
| --- | --- | --- |
| F2 | Internal resolution | Auto (default), 1x, 2x, 3x, 4x, 6x, 8x |
| F3 | Aspect | Original 4:3 (default), experimental Expand, experimental 16:9 |
| F4 | Presentation target | Original (default), display, 60, 120, 144, 240 Hz |
| F5 | Anti-aliasing | Off (default), MSAA 2x, 4x, 8x |
| F6 | Output filter | Nearest, linear (default), pixel scaling |
| F7 | Color precision (restart) | Auto (default), standard, high |
| F11 / Alt+Enter | Window mode | Windowed (default), desktop fullscreen |
| F9 | Restore defaults | Also leaves fullscreen; color precision resets on restart |

Auto resolution follows the window using RT64's VI-aware integer scaling.
Unsupported MSAA falls back to off and is reported in the log; the title shows
the requested value. Output filtering affects presentation, not texture assets.
Higher precision is an internal framebuffer setting, not HDR monitor output.

Widescreen expansion and non-original presentation targets are experimental.
WDC currently supplies raw RDP triangles without camera matrices or persistent
object transforms. These controls do **not** add a wider camera view, HUD
repositioning, or interpolated motion. They may produce incorrect framing or
extra presentation work without smoother motion. Use original aspect and rate
for normal play. Guest simulation and VI timing are unchanged. See the
[capability review](graphics-capabilities.md) for the remaining game-specific work.

For custom targets, edit the file while the player is closed. Schema version 1:

```json
{
  "version": 1,
  "resolution": 0,
  "aspect": 0,
  "rate": 0,
  "samples": 1,
  "filtering": 1,
  "precision": 0,
  "fullscreen": false
}
```

Resolution accepts 0 (auto) or 1-8; aspect accepts 0 (original), 1 (expand),
2 (16:9); rate accepts 0 (original), -1 (display), or 30-360 Hz; samples accepts
1, 2, 4, 8. Filtering accepts 0 (nearest), 1 (linear), 2 (pixel scaling);
precision accepts 0 (auto), 1 (standard), 2 (high). Unknown enum values use
defaults, numeric ranges are bounded, and wrong types reject the file.

Development environment variables should be prefixed `WDC_`, documented here,
and bounded or disabled by default. Temporary probes must state their removal
condition and must not become periodic logging on timing-sensitive threads.

Windows build and player scripts accept `-RomPath`; the build imports the dump
under the expected filename without overwriting a different existing file.
Linux generation currently requires the default filename at the repository root.

- `WDC_MINGW_BIN`: optional Windows compiler directory; otherwise use PATH.
- `WDC_PYTHON_SCRIPTS`: optional native Python CMake tools directory.
- `WDC_DIAGNOSTIC=1`: exit after 15 seconds with diagnostic status and code 2.

The player stores runtime data under ignored `build/user-data`. Input settings
and a controller-navigable frontend remain future work.

Controller Pak saves are enabled on controller port 1 and stored as a raw
32 KiB image at `build/user-data/saves/worlddriverchampionship.us.mpk`.
The game owns note names, allocation, repair and save data. Only a missing
image is formatted automatically. An unreadable or incorrectly sized file is
preserved and reported as an absent Pak. Back up the image while the player is
closed before using the game's Pak manager to delete or repair notes.

Each successful block write is published through a temporary file and atomic
replacement before returning to the game, so Escape does not discard pending
saves. A failed write returns a device error and preserves the previous disk
and memory image. Additional controller ports and Rumble Paks remain unavailable.
