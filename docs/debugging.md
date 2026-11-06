# Debugging

Use a disciplined loop:

1. reproduce from a fresh boot and record ROM, build, renderer, scene, and
   configuration;
2. minimise to one function, table, display list, task, or input transition;
3. state a falsifiable hypothesis;
4. instrument the smallest seam and compare against the real ROM where useful;
5. fix the owning module or export a narrow dependency patch; and
6. rerun the focused regression and the original bounded scenario.

The actual ROM is ground truth for translated behaviour. Read ROM bytes before
reasoning about an address; use `tools/rom/` for callers, references, and
disassembly. Optional ares DebugServer tooling belongs outside version control.

Record stable findings in `docs/reference/rom.md` with a short method and a
quick re-verification command. Keep unresolved theories in the issue or pull
request instead of turning them into permanent comments.

## Development save states

F7 saves a RAM checkpoint; F8 restores it. The default slot is
`build/user-data/wdc_savestate.wstate`, relative to the working directory.
Shortcuts fire on key-down, ignore repeat and are disabled while the settings
overlay captures input. Each request is logged immediately. Save and load
wait for 90 consecutive frames in a menu or race with no fade-out request;
at 30 fps this is about three seconds. Park and let the race finish loading
before saving. If both requests are pending, load wins.

After restarting, enter the same scene/race setup before loading. A race load
requires the same participant/model grid, course/layout, mirror, laps, mode,
dispatcher stack address, live thread/queue layout, and race frame-rate mode.
`WDC_WARP` is the convenient way to repeat a setup; a state does not bypass its
loader. A rejected load leaves RAM alone and logs its reason; a thread-layout
rejection can be retried with F8.

| Environment variable | Meaning |
| --- | --- |
| `WDC_STATE_FILE` | Override the F7/F8 slot path. |
| `WDC_STATE_SAVE` | Save once at the first settled race frame. |
| `WDC_STATE_LOAD` | Load once at the first settled race frame; enter the same setup first. |
| `WDC_STATE_SAVE_DELAY` | Additional settled race frames before automatic save, default 0. |
| `WDC_STATE_LOAD_DELAY` | Additional settled race frames before automatic load, default 0. |

Delays accept decimal integers 0..1000000. Paths may be absolute or relative;
their parent directories must exist. Automatic requests fire once, including
on failure. Manual requests use the slot independently. For example, capture
a settled Hawaii practice race, then restore it after restarting:

```powershell
$env:WDC_WARP = '1:0:practice'
$env:WDC_STATE_SAVE = 'build/user-data/hawaii.wstate'
$env:WDC_STATE_SAVE_DELAY = '120'
./play.ps1
Remove-Item Env:WDC_STATE_SAVE
$env:WDC_STATE_LOAD = 'build/user-data/hawaii.wstate'
./play.ps1
```

The versioned file holds low 8 MiB word-swapped RDRAM, scene/callback/stack
identity, a native-thread address roster, the guest count-clock sample and a
checksum covering the header and payload. Native OSThread context pointers
are stripped before writing.
The loader checks the whole file and live roster in scratch memory, repairs
the pointers to this process's contexts, then copies RAM without yielding.
All translated `osGetCount` calls use a shared virtual epoch, restored with
RAM so saved clock anchors retain their age. Slot publication replaces a
complete temporary file atomically and preserves the old slot on failure.
States and temporary files are ignored by Git; they contain ROM-derived data.

This follows the peers' RAM-checkpoint approach, not an emulator's complete
machine state. Native stacks/register files, RT64/GPU work, queued host audio,
external OS messages, timer state, host enhancement settings, Controller Pak
backing storage and expanded host allocations are not restored. Other guest
threads are cooperatively parked during the copy, but native readers are not
paused. The settle/roster checks reduce risk; they do not prove idle background
work or deterministic recovery. A renderer or audio glitch, hang or crash can
still require restarting and re-entering the saved setup. A hung dispatcher
cannot process F8. Keep checkpoints from a known working run and recreate
them after guest-layout, hook or runtime changes; the format is local to the
USA port and 64-bit little-endian hosts.
