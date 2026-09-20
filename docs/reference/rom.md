# ROM reference

No WDC ROM facts are confirmed in this scaffold.

## Required first measurements

Record these only after inspecting the actual USA dump:

| Fact | Value | Method |
| --- | --- | --- |
| Filename | pending | local dump supplied by developer |
| Header/byte order | pending | first four ROM bytes |
| Size | pending | filesystem and ROM header |
| Hash | pending | exact command and algorithm |
| CPU entrypoint | pending | ROM header bytes 0x08..0x0B |
| CPU code ROM range | pending | disassembly and boundary evidence |
| Runtime VRAM base | pending | entrypoint/code mapping |
| Audio microcode | pending | RSP task observation and ROM scan |

The checked-in `rom-analysis.toml` is the hand-maintained input for these
measurements. Do not fill it with borrowed values from AeroGauge or Lamborghini.
Every address used by a hook must include width, endian convention, owner,
thread, and a command or fixture that can re-check it.
