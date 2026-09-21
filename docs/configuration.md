# Configuration

The final host configuration schema will be defined after the first translated
boot. Keep persistent settings host-owned and make reads/writes thread-safe.
Do not make a menu thread mutate a runtime-owned configuration object without a
snapshot or synchronized hand-off.

Development environment variables should be prefixed `WDC_`, documented here,
and bounded or disabled by default. Temporary probes must state their removal
condition and must not become periodic logging on timing-sensitive threads.

Windows build and player scripts accept `-RomPath`; the build imports the dump
under the expected filename without overwriting a different existing file.
Linux generation currently requires the default filename at the repository root.

- `WDC_MINGW_BIN`: optional Windows compiler directory; otherwise use PATH.
- `WDC_PYTHON_SCRIPTS`: optional native Python CMake tools directory.
- `WDC_DIAGNOSTIC=1`: exit after 15 seconds with diagnostic status and code 2.

The player stores runtime data under ignored `build/user-data`. It has no
persistent graphics/input settings UI yet; output is native-resolution 4:3.
