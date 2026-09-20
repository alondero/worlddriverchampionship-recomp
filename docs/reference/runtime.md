# Runtime reference

The intended runtime path is the shared N64ModernRuntime implementation:
translated guest threads and RDRAM are owned by librecomp/ultramodern, with
RT64 receiving graphics work and the host owning SDL presentation.

The WDC-specific page will record scheduler, VI, file, save, and thread facts
after a real boot. Until then, do not assume a sibling game's scene addresses,
thread priorities, task cadence, or save layout.

Runtime patches must be listed in `patches/README.md`, applied by the scripts,
and accompanied by a focused reproducer or test. A patch that is useful to a
general upstream project should also be compared with upstream before being
kept locally.
