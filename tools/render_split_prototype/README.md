# Render split prototype (throwaway)

Question: can the real race drawing code produce additional frames while input
sampling and simulation remain at their original fixed rate, and which state
must be separated before those frames can be interpolated?

The experimental branch also runs interpolation in the ordinary player.
Select an interpolated presentation rate in Graphics or set
`WDC_EXPERIMENTAL_DISPLAY_HZ=90` before starting a single-player race
with Native 60 fps disabled. The custom setting accepts millihertz precision.
The environment override accepts exact ratios. Integer and fractional targets above 30 Hz
through 1000 Hz are accepted, including `60000/1001`. The ROM keeps its
30 Hz simulation interval. The player draws intermediate camera, car body,
wheel and shadow poses on a private copy of the race sample.

For a bounded run, set `WDC_DIAGNOSTIC=1`, `WDC_WARP=1:0` and
`WDC_WARP_AT=480:2:0:arcade:3`. The diagnostic exits with code 2 after
15 seconds and reports elapsed time, consumed samples and RT64 admissions
per 60 draw groups. Use `WDC_RENDER_STATS=1` for the same counters in a run
without the diagnostic timeout. The probe below supplies separate swap and pixel checks.
Split-screen, unsupported scenes and the native 60 Hz simulation use their
ordinary drawing path.

`player_smoke.py` exercises the saved Graphics setting in the regular player.
It checks the selected rate, simulation sample counts and RT64 admissions in
both races. It requires RT64 to accept retrace presentation between races and
fails if RT64 accepts a retrace frame after swap presentation has taken over
and before it is released. Each run uses a separate temporary settings
directory and retains the diagnostic log.

```powershell
python -B tools/render_split_prototype/player_smoke.py build/worlddriverchampionship_modern.exe "World Driver Championship (USA).z64" --rate 90 --resolution 1
python -B tools/render_split_prototype/player_smoke.py build/worlddriverchampionship_modern.exe "World Driver Championship (USA).z64" --rate 59.94 --resolution 1
```

### Live transitions and recovery

Use a fresh working directory and a fresh shell for each target. The following
PowerShell example starts a three-lap Arcade race at 90 Hz from the repository
root. Repeat with 59.94 in the graphics settings. Let the countdown finish
before sending Start.

```powershell
$player = (Resolve-Path build/worlddriverchampionship_modern.exe).Path
$rom = (Resolve-Path 'World Driver Championship (USA).z64').Path
$work = Join-Path $env:TEMP "wdc-transitions-$([guid]::NewGuid())"
New-Item -ItemType Directory "$work/build/user-data" -Force | Out-Null
'{"version":2,"rate":90,"resolution":1,"interpolate_races":true}' |
    Set-Content -Encoding ascii "$work/build/user-data/graphics.json"
Remove-Item Env:WDC_EXPERIMENTAL_DISPLAY_HZ -ErrorAction SilentlyContinue
$env:WDC_WARP = '1:0:arcade:3'
$env:WDC_RENDER_STATS = '1'
$env:WDC_AUDIO_STATS = '1'
Push-Location $work
try { & $player $rom *> player.log } finally { Pop-Location }
```

| Action | Evidence to retain |
| --- | --- |
| Press Enter to pause, wait a few seconds, then press Enter to resume | Swap release, a retrace admission while paused, and a fresh first group after resuming. |
| Save with F7, drive forward, then load with F8 in the same race | A successful load followed by a checkpoint release and a fresh group at the restored sample. Record any rejected loads and retry when settled. |
| Minimize and restore the focused race window | SDL focus loss and gain, followed by continued swap admissions and advancing simulation samples. |
| Select Restart Race with X from the native pause menu | A loader callback and a new race at sample zero, with retrace during the handoff. |
| Apply Original, then Interpolate to custom at a different rate during the race | Retrace admissions at Original and a fresh first group at the new target. |
| Start Practice Laps through the native Quick Race menus, exit from its pause menu and enter another race | Menu callback `8004e950` with retrace presentation, then a fresh race group and swap admissions. |

In each settled race window, check for roughly 30 simulation samples per
second, explicit admissions and zero retrace admissions. Record PCM submissions,
playback state and rebuffer messages before and after each action. These
counters measure admissions to RT64 and PCM accepted by SDL. Compositor
timestamps, moving pixels and recorded output audio can qualify delivery
and fidelity separately.

For the native menu cycle, clear `WDC_WARP` before boot. Choose Quick Race,
Practice Laps and Hawaii, accept automatic gears, then use Exit Practice in
the pause menu. To script re-entry, set `WDC_WARP_AT=2400:2:0:arcade:3` before
launching and return to the menu before VI 2400. The developer shortcut that
boots directly into Arcade can leave a Quick Race car record uninitialized
and crash after Exit Arcade Race, including at Original rate.

The [testing notes](../../docs/testing.md#high-refresh-transitions) record
the Windows runs and host-test coverage for this sequence.

## Diagnostic builds

The Python builders in this directory create disposable instrumentation binaries.
In particular, fast_hash_build.py, present_audit.py, rt64_profile.py and
draw_boundary.py modify private copies of pinned sources and relink test
objects using the reference build's compile commands. Their outputs stay in
scratch directories. Neither player build script nor release packaging calls
these transforms. Production dependency changes belong in patches/.

## Frozen-snapshot comparison

Use a Windows MinGW/Ninja reference build and its compile database. A
three-lap arcade grid with held acceleration exercises car and view state.
The probe compares repeated ordered triangle headers, forces swap submission,
and captures images for comparison inside each frozen sample.

```powershell
python -B tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode shadow-rate --require-repeatable --present-hz 75 --warp "2:0:arcade:3" --settle-frames 240 --hold-accelerator --audit-present --explicit-present --no-owned-readback --write-watch-reset --verify-write-watch --capture-frames --timeout 180
```

Use `run.py --help` for the diagnostic modes and gates. The [prototype PR](https://github.com/alondero/worlddriverchampionship-recomp/pull/61) records the experiments, controls and measurements.

## Fixed-rate driving comparison

With a Windows MinGW/Ninja player build and its compile database, run:

```powershell
python -B tools/render_split_prototype/verify_gameplay.py --reference-build build --rom "World Driver Championship (USA).z64"
```

The fixture creates a practice checkpoint and boots comparison trials with the same
pending-task layout. After restoring, each trial uses the configured player
draw path. Port zero holds the accelerator with neutral steering. A private
copy of the translated dispatcher records the simulation sample number and all
16 words of every car transform immediately after each physics call at
`8000C6E8`. The fixture requires player movement, a stable roster, 180 consecutive samples, and exact
agreement between two stock trials and the 90 and 144 Hz trials. Each trial's
log and binary trace remain in the printed temporary directory.
Use `--rates 59.94 90 144` to compare additional saved custom targets.
Each requested rate must have millihertz precision and uses the same checkpoint
and stock controls. The trace comparison checks simulation trajectory.
Presentation cadence requires the separate display measurements below.
Use `--warp 2:0:arcade:3` to create an eight-car arcade checkpoint.
The binary trace begins with a little-endian u32 car count, followed by
180 records containing a u32 sample counter and sixteen raw u32 transform
words per car in roster order.

## Display measurements

On X11, run the observer on the same display as the game and provide its window ID.

```sh
python3 tools/render_split_prototype/xpresent.py "$window" --warmup 2 --seconds 10 --crop 280 180 280 128
```

Pixmap completions count presents. The optional crop samples moving scenery
asynchronously and reports changes per elapsed sampling second. For native
Gamescope WSI, preserve the session environment and use WAYLAND_DEBUG=client
to collect wp_presentation_feedback.presented timestamps and discarded
events for the game surface. Exclude startup and report the observation
window, interval percentiles, resolution and track.
