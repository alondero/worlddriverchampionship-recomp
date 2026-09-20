#!/usr/bin/env python3
"""Check required documentation and local Markdown links without a build."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    "README.md", "BUILDING.md", "CONTRIBUTING.md", "CLAUDE.md",
    "docs/README.md", "docs/architecture.md", "docs/testing.md",
    "docs/debugging.md", "docs/configuration.md", "docs/controllers.md",
    "docs/glossary.md", "docs/peer-projects.md",
    "docs/reference/rom.md", "docs/reference/runtime.md",
    "docs/reference/renderer.md", "docs/reference/audio.md",
    "patches/README.md",
]
LINK_RE = re.compile(r"\[[^\]\n]+\]\(([^)\n]+)\)")
DRIVE_PATH_RE = re.compile(r"(?<![\w])(?:[A-Za-z]:[\\/]|\\\\)")


def main() -> int:
    errors: list[str] = []
    for name in REQUIRED:
        if not (ROOT / name).is_file():
            errors.append(f"missing required document: {name}")

    markdown = sorted(ROOT.rglob("*.md"))
    for path in markdown:
        # Dependency submodules are vendored build inputs, not this repository's
        # documentation contract. Their links and platform examples are owned
        # upstream and commonly refer to files omitted from a shallow checkout.
        if any(part in {"build", "build-android", ".git", "lib"} for part in path.parts):
            continue
        contents = path.read_text(encoding="utf-8", errors="replace")
        if DRIVE_PATH_RE.search(contents):
            errors.append(f"machine-specific drive path in {path.relative_to(ROOT)}")
        for raw in LINK_RE.findall(contents):
            target = raw.strip().split("#", 1)[0]
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = unquote(urlsplit(target).path)
            candidate = (path.parent / target).resolve()
            try:
                candidate.relative_to(ROOT.resolve())
            except ValueError:
                errors.append(f"link escapes repository: {path.relative_to(ROOT)} -> {raw}")
                continue
            if not candidate.exists():
                errors.append(f"broken link: {path.relative_to(ROOT)} -> {raw}")

    if errors:
        print("Documentation checks failed:", file=sys.stderr)
        print("\n".join(f"- {error}" for error in errors), file=sys.stderr)
        return 1
    print(f"Documentation checks passed for {len(markdown)} Markdown files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
