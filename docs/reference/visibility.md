# Distance and detail

The verified USA ROM makes visibility decisions before BOSS projects triangles.
RT64 receives low-level RDP batches, so its high-level display-list LOD settings
cannot restore omitted cars or track sections. The enhancements are owned by
`src/game_visibility.cpp`, with injection sites in `rom-symbols.toml`.

## Car detail and pop-in

`func_800218F0` is called for each car by `func_800108B4`.
It rejects camera depth (`car+0x624`, float32 world units) greater than
`scene+0x60` at `0x800219F0`, depths below -1.5 at `0x80021A08`, and cars
outside the horizontal camera extent at `0x80021A34`/`0x80021A4C`.
Beyond 32 units, cars with track-section metadata must also belong to the
current submitted section list (`0x80021A8C` through `0x80021B28`).

The detail selector divides `2.5 * scene+0x98` by camera depth above one unit
and stores the projected-size value at `car+0x9B0`. It advances through the
8-byte rows at `0x8007E880`, storing the selected row at `car+0x9B4`.
The float32 thresholds are 64, 42, 30, 27, 20, 16, 14, 6, 2, followed by a
zero sentinel. Row flags reduce detail and the last row's `0x200` flag rejects
the car entirely. Thus a car can disappear even inside the far plane.

**Full car detail** changes only the comparison operand at `0x80021BE8`,
keeping row zero. The real projected size remains available to fading and
packet generation. It preserves cockpit, near/side and section rejection.
**Draw distance** multiplies the far comparison operand at `0x800219F0` and
the matching projection input at `0x80011DE8`. The latter is subsequently
multiplied by 32 by the ROM. It does not modify the near plane or simulation.

## Track detail and section visibility

`func_800230D4` buckets camera depth at 48-unit intervals into indices 0..6,
then remaps for mode/secondary views. The index selects the packet detail mask
(`0x100 << index`) and the near/distant track material root.
**Full track detail** forces index zero at `0x80023244`, after remapping.

`func_8002343C` starts at the camera/car section, reads forward/backward counts
from section bytes `+0x1C/+0x1D`, and follows u32 links at `+0/+4`.
Some modes reduce these counts. At `0x80023674`, **Draw distance** scales the
resulting counts by 1..4, retaining the ROM's order and subsequent bounding-box
culling. This addresses section-boundary pop-in separately from polygon LOD.

The guest list starts at `0x800B2880`; matrix scratch starts at `0x800B2980`.
There are only 64 pointer slots. The hook validates the original linked walk,
then alternates forward/backward extensions within that capacity, stopping
before invalid links or repeated sections. It only changes count registers;
the ROM still writes and culls the list. Malformed original walks are left
unchanged. Guest memory is word-swapped, game-owned, and read only on the
calling guest render thread; no pointers survive a call. See the source for
the full bridge contract and removal path.

## Evidence and limits

`wdc_visibility_rom` runs extracted, unmodified translated car and track
functions against ROM constants and synthetic RDRAM. It retains the actual
decision branches, track depth arithmetic and list writer. Car pose updates,
matrix setup for list selection, and packet allocation are explicit test
boundaries; this fixture does not execute BOSS or validate final pixels.

The original car routine selected rows 0/3/6/7 at depths 2/10/40/100 with a
100-unit size input, disappeared beyond 125 through its last detail row,
and rejected depth 201 with a 200-unit far plane. The full-detail assertion
failed against the original translation. With hooks it retains row zero,
respects 1x/4x far limits, retains near/side/section rejection and restores
stock behavior when disabled. Track checks cover mode remapping, 17 -> 33 ->
64 section expansion, short-ring deduplication and invalid extension links.
`wdc_camera_rom` checks projection changes; configuration/frontend tests cover
persistence and independent Apply/Discard behavior.

A Windows/MinGW D3D12 smoke check exercised the Enhancements tab in a Hawaii
arcade race, applied both detail options and 4x distance, confirmed the saved
settings, and drove for 15 seconds. The player continued rendering without a
crash or logged runtime error. This was not a full-lap or performance test.

Reproduce with the accepted local ROM:

```text
python -B scripts/gen_syms_toml.py
./build.ps1
cmake --build build --target wdc_visibility_rom_tests wdc_camera_rom_tests wdc_graphics_config_tests wdc_frontend_settings_tests
ctest --test-dir build --output-on-failure
```

These options do not promise removal of every scenery transition: the list
remains bounded, authored geometry and bounding-box culling still apply, and
fog is unchanged. Higher detail/range increases CPU, BOSS and GPU work.
All options default to stock behavior. Visual/performance acceptance across
all tracks, split-screen and full laps remains separate from the ROM tests.

## Sibling comparison

[AeroGauge's car hooks](https://github.com/alondero/aerogauge-recomp/blob/d4408065e88ab089f2c3e934370353e638f06e22/src/aero_car_lod.c)
separate detail selection from visibility and retain near/angular rejection.
Its ROM-backed tests supplied the model for testing translated WDC decisions.
[Lamborghini's visibility policy](https://github.com/alondero/automobililamborghini-recomp/blob/ff7ba22ec7344acdc6b8eeb0d0f96532e58b0019/src/lambo_no_lod_policy.cpp)
retains authored section ordering and bounds synthesized lists, including
track-specific exclusions. WDC instead extends its own linked section walk;
no sibling addresses, assets or exclusion rules are reused. This comparison
was source review, not sibling gameplay validation.
