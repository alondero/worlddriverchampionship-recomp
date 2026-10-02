# First-playable evidence

## Verified milestone

On 2026-09-21, the Windows x64 MinGW/D3D12 player using the verified USA dump:

- Started nine guest threads and completed 444 graphics tasks in a bounded
  15-second headless test.
- Rendered the Controller Pak prompt, animated 3D intro, title and menus.
- Loaded Quick Race / Practice Laps / Hawaii with the initial yellow car.
- Accepted automatic transmission, accelerated to 43-45 mph in second gear,
  steered on track, and paused/resumed. The lap timer exceeded one minute.
- Replayed the final packaged build with a minimal PATH and reached 47 mph in
  third gear. Escape exited successfully with code 0. Required runtime DLLs
  are copied alongside the executable; no compiler installation is needed to run it.

Screenshots and debugger dumps are local ignored build artifacts, not source
assets. This evidence establishes a first testable build, not completion of
all modes, tracks, laps, or compatibility testing.

Final checks: 3/3 CTest tests, 4/4 Python tests, documentation links, and
whitespace checks passed. Both new dependency patches applied to clean copies
of their pinned source files; repeated build-script patch application succeeded.

## Standards review

Review baseline: session-start commit `ee1778b`, including new/uncommitted files.
Three hard findings were resolved: guest-memory contracts/range checks, SDL
audio-thread ownership, and machine-specific compiler discovery. Audio extraction
also addressed the maintainability suggestion. Follow-up source review found
no remaining blocking standards findings.

## Spec review

No blocking findings against the requested first runnable/testable recompilation
and peer-project research. The DLL packaging concern was verified by launching
the final player with only Windows system directories on PATH. Full race/audio
quality acceptance remains explicitly outside the verified milestone.

Current findings: Standards 0 unresolved; Spec 0 blockers. Source changes remain
local and uncommitted; no branch publication or PR was performed.

## Controls and reproduction

Run `./play.ps1` from the checkout. Controller Pak support is now enabled on port 1.
Enter skips the intro and starts from the title. X selects Quick Race;
Down, X selects Practice Laps; X selects Hawaii; X selects automatic gears.
Hold X to accelerate; Z brakes; arrows/WASD steer; Enter pauses.
Current builds use Escape/F1 for settings; quit from the overlay or close the
window. The earlier milestone's Escape-to-exit behavior is historical.
Space maps Z trigger and right Shift maps R. Gamepad and C-button mappings
have not been implemented.

Controller Pak storage uses a raw 32 KiB MPK image under `build/user-data/saves`.
The ROM-backed SDK test verifies initialization, allocation, writing, reopening
with fresh guest RAM, reading and metadata. Native tests cover failed writes,
guest ranges and malformed-image preservation. See [configuration](configuration.md)
and [peer comparison](peer-projects.md). SDL audio queues word-swapped stereo samples; audio
quality needs listening tests. Visible geometry/texture glitches remain.
The narrow 2D menu presentation was fixed by retaining 4:3 TV output for
the game's 320x480 buffers. Track completion and race opponents have
not been acceptance-tested. Android/Linux have not been validated.

On 2026-10-02, the Windows MinGW/D3D12 player booted with an attached Pak
without the no-Pak warning. A new championship driver `G` accepted the
Speedcraft / '97 Stallion SR C offer and saved into slot 1; the game displayed
"DATA SAVED". After closing normally (exit 0) and starting a fresh process,
Championship / Load Game listed `G`, rank 30, zero events and `00:00:00` race
time. Selecting slot 1 displayed "DATA LOADED".
The raw MPK hash remained unchanged across the fresh boot. Keyboard events
were injected into the SDL window for this menu check; screenshots stay in
the ignored build tree and are acceptance evidence, not regression tests.

## Durable fixes and provenance

See [peer research](research/first-playable.md) for measured ROM addresses and
primary upstream sources. SDK identities are recorded in `rom-symbols.toml`.
All ROM-derived output remains ignored.

- CPU shared-body math helpers need overlapping ranges or native equivalents.
- The strcmp entry has a backward shared loop; the native implementation
  preserves unsigned-byte comparison returning -1/0/+1.
- Startup routine 80037F20 rewrites seventeen instructions into cache
  operations. The source transformer matched all seventeen live-memory
  words exactly, and the full text comparison found no other modifications.
- BOSS requires real SP status signals and intermediate SP_BREAK interrupts,
  COP0 callbacks, CFC2/CTC2, and computed instruction-target dispatch.
- RT64 consumes raw RDP batches, retains partial commands, and emits DP
  completion on full sync. Generic immediate SP/DP completion is disabled
  for this renderer only.
- Unknown privileged CPU operations and unknown RSP tasks fail loudly.

## Next work

Investigate remaining rendering glitches using RDP image/scissor state.
Validate audio, then full laps/opponents, gamepad,
additional tracks, and full race/championship save flows. Do not replace the LLE path
with a generic graphics microcode: WDC uses BOSS, not Fast3D.

Suggested continuation: "Continue from docs/first-playable.md. Reproduce the
Windows Hawaii practice path using play.ps1, investigate the remaining
rendering glitches, and add regression coverage before
changing the renderer or translated microcode."
