# BOSS task capture and replay

`wdc_boss_bench` re-runs one captured BOSS graphics task offline: translated
microcode only, without the game's CPU threads, RT64 or a window. Use it to
measure translated BOSS time and as a regression check for RSPRecomp,
runtime-helper or BOSS-bridge changes. Every iteration must reproduce the
captured RDP command stream byte for byte.

## Capture

```powershell
$env:WDC_BOSS_CAPTURE = "artifacts\boss\hawaii"
.\build\worlddriverchampionship_modern.exe "World Driver Championship (USA).z64"
```

Press F10 during the scene of interest. Each press writes the next graphics
task to `<prefix>-<n>.boss` (about 8.6 MB) and logs the path. Captures contain
ROM-derived RDRAM and stay local; `artifacts/` is ignored.

A capture holds the `OSTask`, the starting SP/DP registers and DMEM, the
RDRAM view BOSS read, every SP_STATUS value it read (run-length), and the RDP
ranges it produced. Replay is only byte-exact because recording:

1. journals each RDRAM byte before BOSS first writes it, then rebuilds the input
   view from RDRAM after the task with those bytes restored (CPU-written
   sublists stay in place);
2. plays the recorded SP_STATUS reads back in order, since BOSS's dispatcher
   polls SIG0 for the CPU's "sublist complete" signal; and
3. holds DP-complete until the RDRAM view is saved, because after that signal
   the game starts overwriting data BOSS already read. Mid-task SP signals are
   still delivered; the CPU must answer them for the task to finish.

The format is defined in `src/boss_capture.cpp` and covered by
`tests/test_boss_capture.cpp`. `wdc_rsp_dma` checks that the journalled range
is exactly what the patched runtime DMA helper writes.

## Benchmark

```powershell
cmake --build build --target wdc_boss_bench
.\build\wdc_boss_bench.exe artifacts\boss\hawaii-0.boss --iterations 500
```

The tool runs one untimed warm-up and then the timed iterations, restoring the
captured RDRAM before each. It exits 0 only if every iteration ends with BREAK,
consumes exactly the recorded status reads and matches the recorded output.
A mismatch reports the first differing range and byte and exits 1; usage and
file errors exit 2. Each timed iteration covers `replay()` only: the DMEM copy,
translated BOSS and an in-place `memcmp` of each RDP range. It excludes the
RDRAM reset, and replay skips the live path's yield on each status read.

`--iterations` takes a plain count from 1 to 1,000,000. Anything else is
refused: the tool counts up to the limit, so a count near the range of an
unsigned would wrap its counter and never end. A replay must also never
signal the game; the bench counts any event that does and fails the run.

## Limits

- Only RDP output is compared. Other BOSS RDRAM writes, such as matrices
  written back for the CPU, are replayed but not checked.
- Recorded racing tasks so far read SIG0 set once and clear afterwards, so the
  CPU never answered BOSS mid-task. A task with a real ZSort hand-off works
  only if the CPU does not overwrite, during the task, data BOSS already read.
- Guest threads keep running while the 8 MiB view is copied. Like the
  hand-off case above, a write of theirs to data BOSS already read produces a
  capture that fails replay, never one that passes falsely.
- A scene whose CPU waits for DP-complete before answering BOSS would stall
  while recording, because DP-complete is held.

## Profile (Windows)

```powershell
.\build\wdc_boss_bench.exe artifacts\boss\hawaii-0.boss --iterations 3000 --profile artifacts\boss\hawaii-0.samples
python -B tools/boss/profile_report.py artifacts\boss\hawaii-0.samples
```

A sampling thread records the benchmark thread's instruction pointer about every
250 microseconds, only while a replay runs. The report needs MinGW `addr2line` on
`PATH`. It groups samples by IMEM window, by RSP label in the generated
`src/aspMain.cpp`, and by innermost (usually inlined) helper. Optimized line
tables are approximate, so read clusters rather than single labels. Helpers
that are not inlined appear as "outside bossMain".
