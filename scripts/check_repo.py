#!/usr/bin/env python3
"""Reject tracked files covered by the repository's ignore rules."""
from pathlib import Path
import subprocess
import sys


def check(root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "--cached", "--ignored",
         "--exclude-per-directory=.gitignore", "-z"],
        check=True, capture_output=True,
    )
    return sorted(result.stdout.decode("utf-8").rstrip("\0").split("\0")) if result.stdout else []


def main() -> int:
    paths = check(Path(__file__).resolve().parents[1])
    if paths:
        print("Ignored files are tracked; remove them from the index:", file=sys.stderr)
        for path in paths:
            print(f"- {path}", file=sys.stderr)
        return 1
    print("Repository hygiene passed: no ignored files are tracked.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
