# Configuration

## Command-line options

```powershell
.\build\worlddriverchampionship_modern.exe --console --verbose --rom "World Driver Championship (USA).z64"
.\build\worlddriverchampionship_modern.exe --fullscreen --data-dir "profiles/arcade"
```

| Option | Effect |
| --- | --- |
| `--rom PATH` | Load the specified USA ROM. A positional ROM path also works. |
| `--console` | On Windows, attach to the parent console or open one if the player has no output destination. Ordinary launches from Explorer run without a console. |
| `--verbose`, `--log-level LEVEL` | Use debug logging or choose `error`, `warn`, `info`, `debug`, or `trace` for port messages. The player defaults to `info`. |
| `--fullscreen`, `--windowed` | Override the saved window mode for this launch. The last of these options wins. |
| `--data-dir PATH` | Use a different directory for settings, runtime data, saves, and logs. The default is `build/user-data` under the working directory. |
| `--portable` | Use `user-data` beside the desktop executable. Choose this or `--data-dir`. |
| `--controller-pak PATH` | Use this raw 32 KiB Controller Pak image directly for both loading and saving. |
| `-h`, `--help`, `--version` | Print usage or the version and exit. |

Resolve relative paths against the working directory and quote paths with
spaces. Value options also accept `--option=value`. Use `--` before a
positional ROM path that starts with a dash. With no ROM argument, the desktop
player looks for `World Driver Championship (USA).z64` in the working directory
and then beside the executable.

On Windows, `./play.ps1 -Console` enables the console when using the PowerShell
launcher script. See [player logs](debugging.md#player-logs) for the log location
and redirection behavior.

## Custom Grid

Open the settings overlay with **Esc** or **F1** and select **Custom Grid**.
Choose a car and color for each CPU opponent in single-player Quick Race
(Arcade Race) and Championship. Opponent 1 is the CPU car nearest the front
of the starting grid. The list skips your car and uses only as many opponents
as the race has.

**Game-selected car** and **Game-selected color** independently keep the
original choices. **Default color** selects the first palette for that model.
Choices are saved as you make them. Start or restart a race to load them.
**Restore defaults** returns every opponent to the game's selection.

## Developer race warps

Launch directly into any of the 58 course layouts with any of the 35 vehicles,
including locked content:

```powershell
./play.ps1 -Warp '1:0'                 # Hawaii A, model 0, Practice Laps
./play.ps1 -Warp '58:34:arcade:3'       # Last course, Interceptor, 3-lap Arcade Race
./play.ps1 -Warp '47:16:time-attack'    # Zurich BR, Challenger, Time Attack without ghost
```

The cross-platform equivalent is environment variable
`WDC_WARP=track[:car[:mode[:laps]]]`. Track IDs are **1..58** (ROM course id + 1),
vehicle IDs **0..34**. Omitted fields select model 0, `practice`, and the native
lap count. Modes are `practice`, `time-attack`, and `arcade`; only Arcade accepts
an explicit lap count, 2..5, or 0 for the course default. Practice is unlimited.
An Arcade lap count in the warp text is the count for that launch, including
when Quick Race laps is set to another count.
Invalid values stop startup with a diagnostic instead of silently changing the
requested test. Remove the variable to restore ordinary startup.

Print the full ID/name catalog from your local ROM:

```text
python -B tools/rom/warp_catalog.py
python -B tools/rom/warp_catalog.py --json
```

**Ctrl+Shift+R** reloads the last requested setup. **Ctrl+Shift+1..9,0** starts
practice with model 0 on the first layout of Hawaii, Les Gets, Las Vegas,
New Zealand, Lisbon, Rome, Sydney, Zurich, Kyoto, or Black Forest respectively.
These developer shortcuts also work during a race. A request waits through
boot/loading and active transitions; the newest pending request wins.
Startup requests skip boot dialogs and the logo delay while retaining the
original Controller Pak scan and menu initialization.
For a scripted mid-race transition, also set
`WDC_WARP_AT=vi:track[:car[:mode[:laps]]]` (VI 1..2147483647).
For example, `WDC_WARP=1:0` with `WDC_WARP_AT=300:58:34:arcade:3` starts in
Hawaii practice, then queues the Interceptor race at host VI 300. The VI
callback only publishes a request; the game thread performs the transition.

Warps use automatic transmission, no mirror and no ghost, and follow the
original scene cleanup and race loader. They temporarily borrow and restore
the career's setup selectors; they do not unlock content or write a save.
Ordinary race results can still grant the original Quick Race rewards, and
explicit saving afterward can preserve those rewards. Warp selection is
session-only and does not enter settings files.

The bridge and its ROM addresses are documented in
[`src/game_warp.cpp`](../src/game_warp.cpp). Native setup comparisons cover all
course/model/mode combinations; see [testing](testing.md).

## Developer save states

**F7** saves a settled RAM checkpoint and **F8** restores it. The default slot
is `build/user-data/wdc_savestate.wstate`. Set `WDC_STATE_FILE` to select a
different slot, or use `WDC_STATE_SAVE` / `WDC_STATE_LOAD` for one-shot scripted
race checkpoints. After restarting, enter the same race setup before loading.
These development checkpoints do not capture native stacks or renderer work;
see [usage, environment controls and limits](debugging.md#development-save-states).

## Host settings

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
flow. The Controls tab edits the live keyboard and gamepad bindings, including
per-device reset. Bindings save on leaving that tab. General exposes joystick
deadzone and background controller input; changes take effect immediately and
save when leaving that tab.

**Driving** provides independent Digital/Analog modes for throttle and brake,
an SDL controller axis (including None), negative half-axis selection, deadzone
(0.00-0.95) and saturation (0.05-1.00). Defaults are Digital, RT throttle,
LT brake, positive half-axis, 0.05 deadzone and 1.00 saturation. When saturation
is at or below the deadzone, the effective range ends 0.01 above the deadzone.
Driving changes are live, save to `build/user-data/driving-controls.json` when
leaving the tab, and follow the same live **Restore defaults** policy as General.
Digital A/B overrides remain available. See [Controllers](controllers.md) for
trigger binding conflicts and reverse controls.

**Cheats** offers the original game's understood unlocks, name-password
effects, restart credits, rendering effects and cameras for the current
session. Changes apply at the boundary described by each option and are forgotten when
the application closes. These toggles do not write the Controller Pak note.
The measured save routine still sees the real locks and earned credit balance.
Turn an unlock toggle off, then open the
list again, to see the saved locks. **Restore defaults** on that tab turns the
toggles off. A few menu screens also keep a copy of what they displayed;
[Cheats](cheats.md) describes that copy.

Navigate with the mouse, arrows or Tab and Enter. Escape/F1 closes the overlay;
the header also has close and quit buttons. Pause a race with Enter before
opening settings: the game continues running while the overlay is open, but
menu input is blocked from the game. Held gameplay buttons, stick and enabled pedals must be released before
input resumes. Color precision requires a restart; most other changes apply live.

**Graphics → Race frame rate** is the single frame-rate selector. **Original**
is the default. **Native 60 fps (experimental)** raises racing's input sampling
and rendering to 60 Hz together. Apply saves the preference; it takes effect
when the game next starts its input worker (starting a race or returning to the
menus). Changing it during a race leaves that race's rate intact. The original
intro and attract-mode demos keep their authored 30 fps timing, even with this
option enabled. Replay/ghost streams do not store their sampling rate;
recordings are not compatible across the two rates. Keep this option off for
normal play until the experimental acceptance work is complete. See
[rendering and frame rate](rendering.md#native-60-fps-option) for the audit,
measurements and remaining validation.

The interpolated choices keep the original 30 Hz simulation and draw extra
single-player race frames. The legacy Display and Custom choices only change
renderer presentation; they cannot create new intermediate World Driver
Championship motion. Existing settings keep that previous behavior under the
legacy labels rather than silently moving to the more expensive interpolation
implementation.

**Quick Race laps** sets the length of the next Arcade Race or Time Attack.
**Course default** keeps each track's own count and is the default. The choice
is used when that race is set up. Practice stays unlimited. The course select
screen can still show the track's original count. The lap counter during the
race follows the chosen count. When the race is longer than five laps, the
results list still shows five rows. A first-place Arcade finish awards that
car's original prize for a chosen count of 2, 3, 4, or 5 laps. Any other count
awards nothing. The prize follows the car and the chosen count. A Time Attack
ghost recorded for a different number of laps has not been checked.

The window title shows requested settings. Graphics options are reached through
the RT64 frontend overlay, which is the port's only menu and opens in both
windowed and fullscreen modes on Windows, Linux and Android. The shortcuts
below remain available during gameplay; use the overlay's
controls while it is open. F11 / Alt+Enter works in either mode.

| Shortcut | Setting | Choices |
| --- | --- | --- |
| F2 | Internal resolution | Auto (default), 1x, 2x, 3x, 4x, 6x, 8x |
| F3 | Aspect | Match window (default, wider camera), original 4:3 |
| F4 | Race frame rate | Cycles Original, Native 60, interpolated display/custom, then legacy display/custom |
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
windows and fullscreen displays. Cars retain their proportions. **HUD placement**
in the Graphics tab controls the race map, instruments and timing blocks:
Original keeps the centered 4:3 layout; Clamp to 16:9 (default) moves them toward
the edges up to a 16:9 area; Full width follows the output edges. The needle
travels with its dial. Changes apply live and are saved with the other settings.
At 4:3 or narrower output these choices coincide. Menus, pause panels and the
wrong-way message stay centered. The blue menu header and footer bands reach
the window edges by continuing their outer edges; their text and buttons keep
the original layout. The game's outer TV
overscan margins are removed by expanding its viewport and scissor, without
cropping the scene. Cinematic layouts and fixed 2D artwork can still leave
unused space. Narrow windows and Original use a fitted 4:3 view.
Version 1 preferences, which forcibly disabled widescreen, migrate to Match
window. An explicit Original choice in version 2 survives restarts.
Under **Graphics → Race frame rate**, choose **Interpolate to display
(experimental)** to follow the detected monitor rate, or **Interpolate to
custom (experimental)** to set a target. Press **Apply**. If Native 60 fps
was active, start a new race so the input worker starts at 30 Hz. In supported
single-player races the game draws the camera, cars, wheels and shadows between
simulation samples. Split-screen and authored demos use their original draws.
Rendering throughput depends on the scene and hardware.

The legacy **Display** and **Custom** choices set RT64's presentation target.
Because World Driver Championship submits projected raw triangles, those modes
can repeat frames but cannot create real intermediate motion. See the
[rendering notes](rendering.md) for the measured differences.

The overlay offers every resolution multiplier from 1x to 8x and a Custom
presentation target from 30 to 1000 Hz. Interpolated custom rates accept up to
three decimal places, including 59.94 and 119.88 Hz. Existing custom values are retained.
You can also edit the file while the player is closed. Schema version 2:

```json
{
  "version": 2,
  "resolution": 0,
  "aspect": 1,
  "hud": 1,
  "rate": 0,
  "interpolate_races": false,
  "native_60fps": false,
  "custom_rate": 60,
  "samples": 1,
  "filtering": 1,
  "precision": 0,
  "fullscreen": false,
  "full_car_detail": false,
  "full_track_detail": false,
  "draw_distance": 1,
  "quick_race_laps": 0
}
```

Resolution accepts 0 (auto) or 1-8; aspect accepts 0 (original) or 1 (window);
HUD placement accepts 0 (original), 1 (clamp to 16:9), or 2 (full width).
Older version 2 files without `hud` use the default clamp.
The file keeps the version 2 storage keys for compatibility. `rate` accepts
0 (original), -1 (display), or 30-1000 Hz. `interpolate_races`,
`native_60fps`, and `rate` are folded into the single menu mode when loading:
an interpolated rate wins first, then Native 60 fps, then a legacy display or
custom target. `custom_rate` remembers the custom target while a non-custom
mode is selected; the active custom mode also writes its target to `rate` so
older players can read the file. When an old combined Native 60 fps and custom-rate
file migrates, that positive `rate` becomes the inactive custom target.
The remembered custom target keeps up to three decimal places in every mode.
Legacy Custom passes only the nearest whole-number rate to RT64; older players
also round a fractional legacy `rate` when loading it. Samples accepts
1, 2, 4, 8. Filtering accepts 0 (nearest), 1 (linear), 2 (pixel scaling);
precision accepts 0 (auto), 1 (standard), 2 (high). `quick_race_laps` accepts
0 (course default), 1 through 10, 15, or 20. Any other number becomes 0.
`draw_distance` accepts 1 through 8, or 16 for bounded Unlimited. Other values
are clamped to 1 through 8. A missing key uses 0. Unknown enum values use
defaults, numeric ranges are bounded, and wrong types reject the file.

The **Enhancements** tab offers **Quick Race laps**, **Full car detail**, **Full track detail**, and
**Draw distance** (Original, 2x through 8x, or Unlimited). All four follow Apply/Discard.
Quick Race laps is used when the next Arcade Race or Time Attack is set up.
The detail and distance options take effect live. Full car detail retains the nearest detail level and prevents the
lowest-detail row from hiding distant cars. Full track detail retains the
nearest detail level for visible sections. Draw distance extends the far plane,
scenery range and the number of sections shown ahead and behind. Values 2x
through 8x scale the authored section counts. Unlimited follows the section links
until every unique reachable section is submitted or a bounded 1024-entry
safety cap is reached. It is not a mathematically infinite projection plane:
the port uses a very large finite range so camera depth calculations remain
valid. Use Full car detail together with increased distance for distant cars.
Fog, authored scenery behavior, and bounding-box culling remain. Unlimited can
be very expensive because it submits most or all of a course every frame.
The defaults preserve original detail and distance. F9 also resets these
options, including Quick Race laps back to Course default. See [the ROM evidence](reference/visibility.md).
Quick Race laps is described above. Its prize rows are the original car and
lap-count table in [Quick Race rewards](research/original-track-unlocks.md).

Development environment variables should be prefixed `WDC_`, documented here,
and bounded or disabled by default. Temporary probes must state their removal
condition and must not become periodic logging on timing-sensitive threads.

Windows build and player scripts accept `-RomPath`; the build imports the dump
under the expected filename without overwriting a different existing file.
Linux generation currently requires the default filename at the repository root.

- `WDC_MINGW_BIN`: optional Windows compiler directory; otherwise use PATH.
- `WDC_PYTHON_SCRIPTS`: optional native Python CMake tools directory.
- `WDC_DIAGNOSTIC=1`: exit after 15 seconds with diagnostic status and code 2.
- `WDC_AUDIO_STATS=1`: log audio device period/reserve, playback start, PCM submissions,
  empty-queue recovery and backlog-discard events on the SDL main thread.
  Disabled by default; no per-frame logging. See [audio](reference/audio.md).
- `WDC_RDP_CAPTURE=<prefix>`: enable F10 to record the next complete BOSS task's
  raw RDP commands to `<prefix>-<n>.rdp` for `tools/rdp/analyze_seams.py`. Each
  press writes one file on the graphics worker; nothing is recorded otherwise.
  Numbering restarts per run and replaces existing files; a failed write
  deletes the partial capture. Captures contain ROM-derived data and stay local. Remove this probe once RT64
  can record raw RDP batches submitted by BOSS.
- `WDC_BOSS_CAPTURE=<prefix>`: enable F10 to record the next complete BOSS
  graphics task, with its RDRAM view and status-read timeline, to
  `<prefix>-<n>.boss` for the offline `wdc_boss_bench` replay. It can be set
  together with `WDC_RDP_CAPTURE`. Recording holds that task's DP-complete
  signal until the RDRAM view is saved, then writes the file on the graphics
  worker, so the captured frame is late; nothing is recorded otherwise. A
  scene whose CPU waited for DP-complete mid-task would stall while recording;
  Hawaii racing does not. Numbering, replacement and the local-only rule match
  `WDC_RDP_CAPTURE`. See [BOSS task capture](../tools/boss/README.md). Remove
  this probe, `src/boss_dma.h` and `wdc_boss_bench` once BOSS no longer runs as
  translated microcode in this port.

The player stores runtime data under ignored `build/user-data`. Native
RecompFrontend profiles persist in `controls.json`; rumble strength, deadzone and background
input preferences persist in `general.json`. See [Controllers](controllers.md)
for defaults, remapping and [optional SDL mapping files](controllers.md#custom-controller-mappings).
An audio volume control remains future work. The overlay
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
and memory image. Rumble runs alongside this storage on port 1; its strength
defaults to 25%, persists in `general.json`, and zero disables it. Additional
controller ports remain unavailable. See [rumble ownership](reference/input.md#rumble-alongside-saves).

The overlay uses the same RecompFrontend tabbed UI as the Automobili Lamborghini
peer port. Contexts and staged edits belong to the presentation callback;
Apply queues a whole settings snapshot for the SDL main thread to publish,
save and use for window operations. RecompFrontend does not write `graphics.json`.
The Driving, General and Controls tabs use RecompFrontend's native storage and input
library. Cheats stay in memory for the session and are not written to a settings
file. Profile edits and SDL sampling serialize with presentation; guest
controller callbacks read only a coherent host snapshot.
The host also queues the frontend's cursor, clipboard, mouse capture and dialog
operations to SDL's main thread. Font/icon assets are loaded beside the executable.
