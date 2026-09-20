# Configuration

The final host configuration schema will be defined after the first translated
boot. Keep persistent settings host-owned and make reads/writes thread-safe.
Do not make a menu thread mutate a runtime-owned configuration object without a
snapshot or synchronized hand-off.

Development environment variables should be prefixed `WDC_`, documented here,
and bounded or disabled by default. Temporary probes must state their removal
condition and must not become periodic logging on timing-sensitive threads.

The build scripts use `ROM_FILENAME` on Linux/CI and `-RomPath` on Windows.
These select the local input ROM; they are not player configuration.
