# Runtime reference

The intended runtime path is the shared N64ModernRuntime implementation:
translated guest threads and RDRAM are owned by librecomp/ultramodern, with
RT64 receiving graphics work and the host owning SDL presentation.

## Input-worker startup ordering

The USA initializer `func_8002CB0C` creates thread 9 (entry `0x8002C500`,
priority 11, OSThread at `0x800B7620`). The ROM starts it at `0x8002CCF4`
before initializing the eight input snapshots at `0x800B97E8`, stride `0x200`.
Each snapshot contains four `0x7C`-byte controller records; `func_8002C494`
and `func_800036CC` set their pad pointers and snapshot back-pointers.

A Windows boot reproduced a preemption before that loop: the worker reached
`func_80002F58` with snapshot `0x800B97E8` and a null pad pointer at offset
`0x74`, then faulted reading the pad's status byte. All eight snapshots were
still uninitialized, and the creator was suspended in `osStartThread`.
Other boots passed this point, making splash-screen failure timing dependent.

`rom-symbols.toml` suppresses the early start instruction and calls the same
OS entry at `0x8002CD20`, after the initialization loop and before the creator's
epilogue. The generator verifies the replaced instruction against the ROM.
The scheduler and input-reading code retain their existing behavior.

`wdc_startup_rom` executes the generated initializer and both real ring
initializers. Its OS-start boundary checks all 32 pad pointers and back-pointers
immediately, modeling a worker that preempts its creator. It checks fresh
initialization and reinitialization, exactly one start per creation, the worker
identity, and preserved stack/callee-saved registers. The original generated
code fails this test before the first worker start completes. Run it with
`ctest --test-dir build -R wdc_startup_rom --output-on-failure` after regeneration.

On 2026-10-23 the regenerated startup was compiled and linked into the Windows
D3D12 player using the existing dependency/build objects. Three fresh
15-second diagnostic runs passed the transition and exited with the expected
diagnostic code 2; a separate run stayed alive for several minutes and rendered
the 3D intro. The focused startup regression, 17 existing CTest entries, nine
Python tests, documentation, repository-hygiene and whitespace checks passed.
This was an incremental player rebuild, not a clean build or full-race test.

Runtime patches must be listed in `patches/README.md`, applied by the scripts,
and accompanied by a focused reproducer or test. A patch that is useful to a
general upstream project should also be compared with upstream before being
kept locally.
