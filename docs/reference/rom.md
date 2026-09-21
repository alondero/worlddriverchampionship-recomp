# ROM reference

The USA dump has been measured and validated for the first playable.

## Confirmed measurements

These values come from the supplied dump and its startup/task code:

| Fact | Value | Method |
| --- | --- | --- |
| Filename | World Driver Championship (USA).z64 | local dump supplied by developer |
| Header/byte order | 80371240, big-endian | first four ROM bytes |
| Size | 0x1000000 (16 MiB) | byte count |
| SHA-256 | e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97 | Python hashlib.sha256 |
| CPU entrypoint | 0x80000400 | ROM header bytes 0x08..0x0B |
| CPU code ROM range | 0x1000..0x7D2B0 exclusive | disassembly, followed by RSP boot |
| Runtime mapping | VRAM = ROM + 0x7FFFF400 | startup mapping |
| Combined graphics/audio microcode | BOSS, ROM 0x7D380, size 0xF60 | task builder 0x80024F08 |

The checked-in `rom-analysis.toml` is the hand-maintained input for these
measurements. Do not fill it with borrowed values from AeroGauge or Lamborghini.
Every address used by a hook must include width, endian convention, owner,
thread, and a command or fixture that can re-check it.

Detailed evidence and shared-tail/runtime-rewrite findings are in
[peer research](../research/first-playable.md).
