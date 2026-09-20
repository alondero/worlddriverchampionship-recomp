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
