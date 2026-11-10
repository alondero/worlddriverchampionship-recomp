# Render split prototype (throwaway)

Question: can the real race drawing code produce additional frames while input
sampling and simulation remain at their original fixed rate, and which state
must be separated before those frames can be interpolated?

This is a native experiment, not a shipping graphics option. It relinks an
already-built Windows/MinGW player with a small probe. The reference build and
generated code are read-only; no submodule or player source is patched. GNU
linker wrappers intercept the dispatcher, two sound-list boundaries, task
submission and BOSS output. Runtime counters check interception actually took
place. The executable, settings, logs and ROM-derived observations stay under
ignored `build/render-split-prototype/`.

## Run

Requires a completed Ninja/MinGW player build with its object files, static
libraries, dependencies and DLLs still present. Use the same compiler/toolchain
as that build. Supply your own validated USA ROM:

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode split60
```

`--reference-build` may point to an existing build in another checkout. The
probe uses **that build's** game/runtime/renderer, not unbuilt source changes
in this worktree. It compiles only `probe.cpp` and relinks into a separate
executable. It does not perform a full player rebuild.

The run opens Hawaii practice at 1x resolution, waits for 60 eligible race
frames, performs six trials, drains the last extra graphics task through the
normal game path, then exits. The launcher kills the process after 60 seconds
if it does not finish. No controller input is required. It uses separate local
settings/save directories and does not load the user's normal saves.

| Mode | Experiment |
| --- | --- |
| `raw` | One additional original draw, including sound-list side effects |
| `sound-gated` | Skip sound-list rollover and consumption only on the extra draw |
| `repeat` | Two consecutive sound-gated extra draws without simulation between them |
| `camera-offset` | Sound-gated extra draw with camera eye X temporarily increased by one world unit; restore its three position words afterward |
| `split60` | Sound-gated extra draw and one-VI presentation waits for normal/extra draws; leave input sampling at 30 Hz |

Each mode writes `run.log`, `report.json` and `geometry.json` under a fresh
`<mode>/run-*/` directory, retaining prior runs and starting with fresh settings
and Controller Pak storage. Run one probe process at a time. Reports
include sample counts, divider/tick pair, sound buffer selection, elapsed
times and all changed words in the first car record. The launcher rejects
missing trials, failed interception, changed sampling policy, simulation
sample advancement during extra draws and incomplete RDP evidence.

## What was observed

Bounded Hawaii practice runs on Windows D3D12/RTX 3080, stock 30 Hz sampling,
1x internal resolution:

- Six `raw` extra draws completed without advancing the consumed simulation
  sample counter. Each flipped the sound-list buffer; each changed car words
  `+B80/+B88/+B90/+B94/+B98/+B9C`.
- `sound-gated` retained the sound-list buffer, with both skipped-call counts
  matching the number of extra draws. The same six car words changed.
- `camera-offset` completed and restored the camera position. Thirteen car
  words changed, adding `+624/+628/+630/+9B0/+A00/+B84/+B8C` to those above.
  Restoring the camera alone therefore does not restore all derived car state.
- In `repeat`, consecutive extra draws produced different triangle counts
  and ordered triangle-header hashes despite no intervening simulation samples. Isolating
  sound calls is not sufficient to establish repeatable geometry.
- `split60` extra draws took about 16.4–16.8 ms in the initial run, compared
  with about 32–35 ms for one stock-wait extra draw. The simulation counter
  advanced once between the normal frames and stayed unchanged during each
  extra draw. Divider 1 and 1,562,500 ticks remained intact. This demonstrates
  a one-VI draw path with fixed 30 Hz sampling, not measured display scanout
  or successful motion interpolation.

`func_80028314` writes the `+B80/+B88` fields and later prepares the `+B90`
geometry data; this gives a concrete place to investigate the remaining
ZSort/arena state. Their complete ownership is not yet established.

The geometry collector tags each task at `osSpTaskStartGo_recomp`, consumes
tags FIFO at BOSS execution and hashes the first eight u32 words of each raw
RDP triangle (edges/coordinates plus opcode/tile/level fields, excluding shading
and texture coefficients).
It parses across RDP chunk boundaries and excludes alternating framebuffer
addresses. Different hashes are evidence of different ordered triangle headers;
they can also differ due to material bits or command order. Different triangle
counts independently establish that the submitted streams differ. This is
**not proof of correct interpolation**. Neither identical pixels nor a
visual-quality comparison was measured.

## Limits and next experiment

The prototype does not interpolate cars or camera rotation, run above 60 Hz,
or isolate rendering from authoritative RDRAM. Camera offset is a controlled
perturbation, not interpolation. The six-trial, stationary practice route
does not establish gameplay equivalence, audio fidelity, sustained stability,
or split-screen/other-track behavior. One earlier instrumentation build
crashed; a debugger rerun did not reproduce it. Later bounded runs passed,
but that is not a stability qualification.

Input and graphics workers can run during swap waits. Car differences are
observations across the draw interval, not a complete attribution of every
write. Only the first car is inspected; the sample counter is not a full
simulation-state checksum. Audio synthesis and DMA remain in the original
combined BOSS submission even when sound-event processing is gated. Geometry
collection itself adds work, so these timings are not a performance benchmark.

Next: identify the per-frame ZSort/arena and per-view state responsible for
the consecutive-draw mismatch. Establish repeated, unchanged geometry from
one frozen snapshot before interpolating its camera/vehicle transforms.
Then separate the 60 Hz VI/AI clock from presentation scheduling to test
non-integer display ratios. Do not promote these wrappers into a shipping
option; recover explicit interfaces after the experiment establishes ownership.
