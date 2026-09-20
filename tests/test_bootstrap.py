#!/usr/bin/env python3
"""Source-only checks for the initial repository contract."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path


def check(root: Path) -> int:
    required = [
        "BUILDING.md", "CLAUDE.md", "CONTRIBUTING.md", "CMakeLists.txt",
        ".gitmodules", "rom-analysis.toml", "aspMain.us.toml",
        "scripts/build_android.py", "scripts/check_docs.py",
        "scripts/gen_syms_toml.py", "patches/README.md", "docs/README.md",
    ]
    missing = [name for name in required if not (root / name).is_file()]
    if missing:
        print("missing bootstrap files:", ", ".join(missing), file=sys.stderr)
        return 1
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    for marker in ("N64ModernRuntime", "rt64", "RecompFrontend", "WdcRecompiledFuncs"):
        if marker not in text:
            print(f"CMakeLists.txt is missing dependency marker: {marker}", file=sys.stderr)
            return 1
    return 0


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).parents[1]
    return check(root)


class BootstrapContractTests(unittest.TestCase):
    def test_required_files_exist(self) -> None:
        self.assertEqual(check(Path(__file__).parents[1]), 0)


if __name__ == "__main__":
    raise SystemExit(main())
