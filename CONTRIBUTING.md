# Contributing

The pull request is the review point for changes. Describe evidence,
ownership, tests, generated state, dependency state, and known limits.

Before changing code:

1. inspect the branch, submodule pins, and ROM identity;
2. read the owning source, tests, and build path;
3. reproduce the behaviour when possible;
4. decide whether the change belongs in a generator input, port source,
   dependency patch, test, or stable documentation; and
5. keep uncertain interpretations labelled as hypotheses until measured.

## File ownership

| Path | Owner | Rule |
| --- | --- | --- |
| `src/` | Port | Hand-written host code and narrow game hooks |
| `tests/` | Port | Host, ROM-backed, and bounded end-to-end checks |
| `docs/` | Port | Stable facts, workflows, and evidence links |
| `tools/` | Port | Repeatable analysis and capture tools |
| `patches/` | Port/upstream boundary | Diffs against pinned submodules |
| `rom-analysis.toml` | ROM analysis | Measured code and layout inputs |
| `scripts/gen_syms_toml.py` | ROM analysis | Generator for symbol/config output |
| `*.syms.toml`, `*.us.toml` | Generated | Regenerate; never hand-edit |
| `aspMain.us.toml` | Port/ROM analysis | Hand-maintained RSPRecomp input |
| `RecompiledFuncs/`, `src/aspMain.cpp` | N64Recomp/RSPRecomp | Never edit or commit |

## Guest-memory contract

Every new guest-memory access documents the address, width, units, N64 byte
order, reading/writing thread, owner and lifetime, bounds, failure behaviour,
and the measurement or source-level change that could remove the bridge. An
address alone is not an interface.

## Evidence path

```text
ROM/runtime observation
    -> named function, table, or invariant
    -> generator input or narrow hook
    -> focused test or capture
    -> stable reference/documentation
```

Use `tools/rom/` and the optional ares workflow for ground-truth analysis.
Keep research chronology in the issue or pull request; put only lasting rules
in source or docs.

## Checks

```text
python -B scripts/check_docs.py
python -m unittest discover tests
git diff --check
```

Do not claim a build, game run, visual check, or reference-emulator result that
was not actually performed. Do not commit ROM data, generated output, private
session links, or absolute machine paths.
