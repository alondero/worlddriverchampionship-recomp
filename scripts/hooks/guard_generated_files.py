#!/usr/bin/env python3
"""Block agent edits to files owned by ROM generators or recompilers."""
from __future__ import annotations

import json
import sys
from pathlib import PurePath

GENERATED_NAMES = {
    "worlddriverchampionship.us.toml",
    "worlddriverchampionship.syms.toml",
}


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except Exception:
        return 0
    path = (payload.get("tool_input") or {}).get("file_path") or ""
    if not path:
        return 0
    p = PurePath(path)
    if p.name in GENERATED_NAMES:
        print(
            f"BLOCKED: {p.name} is generator output. Edit rom-analysis.toml or "
            "scripts/gen_syms_toml.py, regenerate, review the diff, and rebuild.",
            file=sys.stderr,
        )
        return 2
    if "RecompiledFuncs" in p.parts or tuple(p.parts[-2:]) == ("src", "aspMain.cpp"):
        print(
            f"BLOCKED: {path} is ROM-derived output. Change a checked-in input "
            "and regenerate instead of editing generated C/C++.",
            file=sys.stderr,
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
