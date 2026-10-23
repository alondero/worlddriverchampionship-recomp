#!/usr/bin/env python3
"""Apply a dependency patch series to a pinned submodule deterministically.

The build scripts have to survive repeated runs. Git's reverse-applicability
probe cannot recognise a patch that is already applied when a later patch of the
same series rewrote the hunk context it matched: patch 0016 replaces the
clipping call that patch 0014 added, so `git apply --reverse --check` fails for
0014 even though its changes are present. That probe reported a conflict and
stopped the build.

This tool therefore owns the paths a series writes. Before applying a series it
restores those paths to the submodule's pinned index state, removes the files
the series creates, and then applies every patch in order. The resulting tree is
always exactly the pinned commit plus the series, so a second run reproduces the
first. Local edits to paths the series does not touch are preserved.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def parse_series(patch_paths: list[Path]) -> tuple[list[str], list[str]]:
    """Return the paths a series writes and the files it creates.

    Both lists are relative to the submodule root and keep series order.
    """
    owned: list[str] = []
    created: list[str] = []
    for patch in patch_paths:
        current: str | None = None
        for line in patch.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("diff --git "):
                parts = line.split()
                if len(parts) < 4:
                    current = None
                    continue
                path = parts[3]
                if path.startswith("b/"):
                    path = path[2:]
                current = path
                if path not in owned:
                    owned.append(path)
            elif line.startswith("--- ") and current is not None:
                if line[4:].strip() == "/dev/null" and current not in created:
                    created.append(current)
    return owned, created


def run_git(submodule: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(submodule), *args], capture_output=True, text=True
    )


def restore(submodule: Path, owned: list[str], created: list[str]) -> tuple[list[str], list[str]]:
    """Restore the paths a series owns to the submodule's pinned state."""
    listed = run_git(submodule, "ls-files", "-z")
    if listed.returncode != 0:
        raise RuntimeError(f"cannot list {submodule}: {listed.stderr.strip()}")
    tracked = {entry for entry in listed.stdout.split("\0") if entry}

    reset = [path for path in owned if path not in created and path in tracked]
    removed = [path for path in created if path not in tracked and (submodule / path).exists()]
    # A series that creates a file the pinned tree already tracks keeps the
    # tracked copy; restoring it keeps the submodule free of deletions.
    reset += [path for path in created if path in tracked]
    if reset:
        result = run_git(submodule, "checkout", "--", *reset)
        if result.returncode != 0:
            raise RuntimeError(f"cannot restore {submodule}: {result.stderr.strip()}")
    for path in removed:
        target = submodule / path
        if target.is_file():
            target.unlink()
        elif target.is_dir():
            raise RuntimeError(f"patch created a directory: {path}")
    return reset, removed


def apply_series(submodule: Path, patch_paths: list[Path]) -> None:
    for patch in patch_paths:
        result = run_git(submodule, "apply", "--ignore-whitespace", str(patch))
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise RuntimeError(f"patch does not apply: {patch}\n{detail}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Apply a dependency patch series.")
    parser.add_argument("--submodule", required=True, type=Path,
                        help="submodule worktree that receives the series")
    parser.add_argument("--patch", required=True, action="append", type=Path,
                        help="patch file, repeated in application order")
    args = parser.parse_args(argv)

    submodule: Path = args.submodule
    # Git resolves a patch path against the submodule directory, so the series
    # is resolved once here instead of in every caller.
    patch_paths: list[Path] = [patch.resolve() for patch in args.patch]
    probe = run_git(submodule, "rev-parse", "--git-dir")
    if probe.returncode != 0:
        print(f"ERROR: not a git repository: {submodule}", file=sys.stderr)
        return 2
    missing = [patch for patch in patch_paths if not patch.is_file()]
    if missing:
        for patch in missing:
            print(f"ERROR: missing patch file: {patch}", file=sys.stderr)
        return 2

    owned, created = parse_series(patch_paths)
    try:
        reset, removed = restore(submodule, owned, created)
        apply_series(submodule, patch_paths)
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    for path in reset:
        print(f"restored {submodule}/{path}")
    for path in removed:
        print(f"removed {submodule}/{path}")
    for patch in patch_paths:
        print(f"applied {patch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
