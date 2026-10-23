"""Keep the two build scripts on the same dependency patch series.

CLAUDE.md requires `build.ps1` and `build.sh` to stay synchronized with
`patches/README.md`. These checks compare the patch series each script applies
per submodule, so a patch added to one script and forgotten in the other fails
here instead of producing two different builds.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = ROOT / "build.ps1"
BASH = ROOT / "build.sh"

PATCH_REFERENCE_RE = re.compile(r"patches/([\w\-.]+\.patch)")
POWERSHELL_ENTRY_RE = re.compile(r'@\{ Sub = "([^"]+)"; Patch = "([^"]+)" \}')
ARRAY_RE = re.compile(r"^[ \t]*(\w+)=\((.*?)\)", re.S | re.M)
CALL_RE = re.compile(r"^[ \t]*apply_patch_series\s+(\S+)\s+(.*)$", re.M)
ARRAY_TOKEN_RE = re.compile(r"\$\{(\w+)\[@\]\}")


def powershell_series(text: str) -> dict[str, list[str]]:
    """Return the ordered patch series per submodule declared by build.ps1."""
    series: dict[str, list[str]] = {}
    for submodule, patch in POWERSHELL_ENTRY_RE.findall(text):
        series.setdefault(submodule, []).append(Path(patch).name)
    return series


def bash_series(text: str) -> dict[str, list[str]]:
    """Return the ordered patch series per submodule declared by build.sh."""
    arrays: dict[str, list[str]] = {}
    for match in ARRAY_RE.finditer(text):
        arrays.setdefault(match.group(1), []).extend(PATCH_REFERENCE_RE.findall(match.group(2)))

    series: dict[str, list[str]] = {}
    for match in CALL_RE.finditer(text):
        submodule = match.group(1)
        patches = series.setdefault(submodule, [])
        arguments = match.group(2)
        for name in ARRAY_TOKEN_RE.findall(arguments):
            patches.extend(arrays.get(name, []))
        patches.extend(PATCH_REFERENCE_RE.findall(arguments))
    return series


def bash_call_count(text: str) -> int:
    """Return how many times build.sh applies a patch series."""
    return len(CALL_RE.findall(text))


class BuildScriptSeriesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.powershell = POWERSHELL.read_text(encoding="utf-8")
        # Join shell line continuations so a call site is one logical line.
        self.bash = BASH.read_text(encoding="utf-8").replace("\\\n", " ")

    def test_both_scripts_declare_the_same_series_per_submodule(self) -> None:
        self.assertEqual(powershell_series(self.powershell), bash_series(self.bash))

    def test_bash_applies_each_submodule_exactly_once(self) -> None:
        # Two runs for one submodule would let the second restore wipe the
        # first run's changes, so every submodule gets a single series.
        self.assertEqual(len(bash_series(self.bash)), bash_call_count(self.bash))

    def test_every_referenced_patch_exists(self) -> None:
        referenced = set(PATCH_REFERENCE_RE.findall(self.powershell))
        referenced |= set(PATCH_REFERENCE_RE.findall(self.bash))
        self.assertTrue(referenced)
        for patch in sorted(referenced):
            self.assertTrue((ROOT / "patches" / patch).is_file(), f"missing patch: {patch}")


if __name__ == "__main__":
    unittest.main()
