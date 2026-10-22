# Configuration

Graphics settings are stored in `build/user-data/graphics.json`. The SDL main
thread loads and saves settings; the graphics thread takes a mutex-protected
copy at display-list/VI boundaries. SDL window operations stay on the main
thread. Saving uses a temporary file and replacement after a successful write.
Malformed files are preserved on load and defaults are used. An explicit
settings change replaces the file; a save failure is reported without losing
the live change.

Press **Escape or F1** to open the settings overlay, including in fullscreen.
The initial window is 1280x720; F11 / Alt+Enter uses the desktop resolution.
The Graphics and Enhancements tabs expose the saved graphics options. Choose values, then press
**Apply**; closing or switching tabs with changes offers Apply/Discard.
**Restore defaults** stages the original choices for the same Apply/Discard
flow. The Controls tab lists the current keyboard bindings.

Navigate with the mouse, arrows or Tab and Enter. Escape/F1 closes the overlay;
the header also has close and quit buttons. Pause a race with Enter before
opening settings: the game continues running while the overlay is open, but
menu input is blocked from the game. Held gameplay keys must be released before
input resumes. Color precision requires a restart; the other changes apply live.

The window title shows requested settings. The Windows **Graphics** menu is
hidden in fullscreen and restored in windowed mode. The shortcuts below
remain available during gameplay; use the overlay's
controls while it is open. F11 / Alt+Enter works in either mode.

| Shortcut | Setting | Choices |
| --- | --- | --- |
| F2 | Internal resolution | Auto (default), 1x, 2x, 3x, 4x, 6x, 8x |
| F3 | Aspect | Match window (default, wider camera), original 4:3 |
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

Match window widens the game camera and horizontal visibility to fit wide
windows and fullscreen displays. Cars retain their proportions; HUD and menu
rectangles keep their original size and are centered. The game's outer TV
overscan margins are removed by expanding its viewport and scissor, without
cropping the scene. Cinematic layouts and fixed 2D artwork can still leave
unused space. Narrow windows and Original use a fitted 4:3 view.
Version 1 preferences, which forcibly disabled widescreen, migrate to Match
window. An explicit Original choice in version 2 survives restarts.
Non-original presentation targets are experimental.
WDC currently supplies raw RDP triangles without camera matrices or persistent
object transforms. Higher targets do not add interpolated motion and may add
presentation work without smoother motion. Use original rate for normal play.
Guest simulation and VI timing are unchanged. See the
[capability review](graphics-capabilities.md) for the remaining game-specific work.

The overlay offers every resolution multiplier from 1x to 8x and a Custom
presentation target from 30 to 360 Hz. Existing custom values are retained.
You can also edit the file while the player is closed. Schema version 2:

```json
{
  "version": 2,
  "resolution": 0,
  "aspect": 1,
  "rate": 0,
  "samples": 1,
  "filtering": 1,
  "precision": 0,
  "fullscreen": false,
  "full_car_detail": false,
  "full_track_detail": false,
  "draw_distance": 1
}
```

Resolution accepts 0 (auto) or 1-8; aspect accepts 0 (original) or 1 (window);
rate accepts 0 (original), -1 (display), or 30-360 Hz; samples accepts
1, 2, 4, 8. Filtering accepts 0 (nearest), 1 (linear), 2 (pixel scaling);
precision accepts 0 (auto), 1 (standard), 2 (high). Unknown enum values use
defaults, numeric ranges are bounded, and wrong types reject the file.

The **Enhancements** tab offers **Full car detail**, **Full track detail**, and
**Draw distance** (Original, 2x, 3x, 4x). These follow Apply/Discard and take
effect live. Full car detail retains the nearest detail level and prevents the
lowest-detail row from hiding distant cars. Full track detail retains the
nearest detail level for visible sections. Draw distance extends the far plane
and the number of sections shown ahead/behind within the game's 64-section
limit. Use Full car detail together with increased distance for distant cars.
Some scenery transitions remain; fog is unchanged. Higher settings can reduce
performance. The defaults preserve original detail and distance. F9 also resets
these options. See [the ROM evidence](reference/visibility.md).

Development environment variables should be prefixed `WDC_`, documented here,
and bounded or disabled by default. Temporary probes must state their removal
condition and must not become periodic logging on timing-sensitive threads.

Windows build and player scripts accept `-RomPath`; the build imports the dump
under the expected filename without overwriting a different existing file.
Linux generation currently requires the default filename at the repository root.

- `WDC_MINGW_BIN`: optional Windows compiler directory; otherwise use PATH.
- `WDC_PYTHON_SCRIPTS`: optional native Python CMake tools directory.
- `WDC_DIAGNOSTIC=1`: exit after 15 seconds with diagnostic status and code 2.

The player stores runtime data under ignored `build/user-data`. Gamepad input,
rebinding, rumble, and an audio volume control remain future work. The overlay
exposes the configuration options currently supported by the port.

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

The overlay uses the same RecompFrontend tabbed UI as the Automobili Lamborghini
peer port. Contexts and staged edits belong to the presentation callback;
Apply queues a whole settings snapshot for the SDL main thread to publish,
save and use for window operations. RecompFrontend does not write `graphics.json`.
An internal General tab supplies framework defaults and is hidden because its
controller options do not affect WDC's keyboard input adapter.
The host also queues the frontend's cursor, clipboard, mouse capture and dialog
operations to SDL's main thread. Font/icon assets are loaded beside the executable.
