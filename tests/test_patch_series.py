"""Exercise the dependency patch series helper on a synthetic repository."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts" / "patch_series.py"

MAIN_BEFORE = "int main(void) {\n    return 0;\n}\n"
MAIN_AFTER_FIRST = "int main(void) {\n    int added = 1;\n    return 0;\n}\n"
MAIN_AFTER_SECOND = "int main(void) {\n    int replaced = 2;\n    return 0;\n}\n"

PATCH_FIRST = """diff --git a/src/main.c b/src/main.c
--- a/src/main.c
+++ b/src/main.c
@@ -1,3 +1,4 @@
 int main(void) {
+    int added = 1;
     return 0;
 }
diff --git a/src/added.h b/src/added.h
new file mode 100644
--- /dev/null
+++ b/src/added.h
@@ -0,0 +1,2 @@
+#pragma once
+int added_symbol(void);
"""

# The second patch rewrites the line the first patch added, so the first patch
# can no longer be reverse-applied. This is the shape of patches 0014 and 0016.
PATCH_SECOND = """diff --git a/src/main.c b/src/main.c
--- a/src/main.c
+++ b/src/main.c
@@ -1,4 +1,4 @@
 int main(void) {
-    int added = 1;
+    int replaced = 2;
     return 0;
 }
"""

PATCH_BROKEN = """diff --git a/src/main.c b/src/main.c
--- a/src/main.c
+++ b/src/main.c
@@ -1,3 +1,3 @@
 int other(void) {
-    return 1;
+    return 2;
 }
"""

# Git quotes a path that contains a space, so the header is not whitespace
# tokenizable.
PATCH_ODD_NAME = """diff --git "a/src/odd name.c" "b/src/odd name.c"
--- "a/src/odd name.c"
+++ "b/src/odd name.c"
@@ -1,3 +1,4 @@
 int odd(void) {
+    int added = 1;
     return 0;
 }
"""


def git(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(repo), *args], capture_output=True, text=True, check=True
    )


class PatchSeriesTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.repo = Path(self._temp.name) / "dependency"
        self.repo.mkdir()
        git(self.repo, "init", "--quiet")
        git(self.repo, "config", "user.email", "test@example.invalid")
        git(self.repo, "config", "user.name", "Test")
        (self.repo / "src").mkdir()
        (self.repo / "src" / "main.c").write_text(MAIN_BEFORE, encoding="utf-8")
        (self.repo / "src" / "kept.c").write_text("int kept(void) { return 0; }\n", encoding="utf-8")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "--quiet", "-m", "pinned")

        self.patches = self.repo.parent / "patches"
        self.patches.mkdir()
        self.first = self.patches / "0001-first.patch"
        self.first.write_text(PATCH_FIRST, encoding="utf-8")
        self.second = self.patches / "0002-second.patch"
        self.second.write_text(PATCH_SECOND, encoding="utf-8")
        self.broken = self.patches / "0003-broken.patch"
        self.broken.write_text(PATCH_BROKEN, encoding="utf-8")

    def tearDown(self) -> None:
        self._temp.cleanup()

    def apply_series(self, *patches: Path) -> subprocess.CompletedProcess:
        command = [sys.executable, "-B", str(HELPER), "--submodule", str(self.repo)]
        for patch in patches:
            command += ["--patch", str(patch)]
        return subprocess.run(command, capture_output=True, text=True)

    def test_series_applies_and_stays_idempotent(self) -> None:
        result = self.apply_series(self.first, self.second)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.repo / "src" / "main.c").read_text(encoding="utf-8"), MAIN_AFTER_SECOND)
        self.assertEqual(
            (self.repo / "src" / "added.h").read_text(encoding="utf-8"),
            "#pragma once\nint added_symbol(void);\n",
        )

        # A second run must succeed and reproduce the same tree.
        again = self.apply_series(self.first, self.second)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual((self.repo / "src" / "main.c").read_text(encoding="utf-8"), MAIN_AFTER_SECOND)
        self.assertEqual(
            (self.repo / "src" / "added.h").read_text(encoding="utf-8"),
            "#pragma once\nint added_symbol(void);\n",
        )

    def test_patch_owned_paths_are_restored_and_others_are_kept(self) -> None:
        (self.repo / "src" / "main.c").write_text("int local(void) { return 9; }\n", encoding="utf-8")
        (self.repo / "src" / "kept.c").write_text("int kept(void) { return 7; }\n", encoding="utf-8")

        result = self.apply_series(self.first, self.second)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.repo / "src" / "main.c").read_text(encoding="utf-8"), MAIN_AFTER_SECOND)
        self.assertEqual((self.repo / "src" / "kept.c").read_text(encoding="utf-8"), "int kept(void) { return 7; }\n")

    def test_patch_that_does_not_fit_the_pinned_tree_fails_loudly(self) -> None:
        result = self.apply_series(self.broken)
        self.assertEqual(result.returncode, 1)
        self.assertIn("patch does not apply", result.stderr)

    def test_missing_submodule_is_reported(self) -> None:
        command = [sys.executable, "-B", str(HELPER), "--submodule", str(self.repo / "absent"),
                   "--patch", str(self.first)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("not a git repository", result.stderr)

    def test_missing_patch_file_is_reported(self) -> None:
        command = [sys.executable, "-B", str(HELPER), "--submodule", str(self.repo),
                   "--patch", str(self.patches / "0004-absent.patch")]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing patch file", result.stderr)

    def test_quoted_path_with_spaces_is_parsed_and_applied(self) -> None:
        odd = self.repo / "src" / "odd name.c"
        odd.write_text("int odd(void) {\n    return 0;\n}\n", encoding="utf-8")
        git(self.repo, "add", ".")
        git(self.repo, "commit", "--quiet", "-m", "odd path")
        patch = self.patches / "0005-odd-name.patch"
        patch.write_text(PATCH_ODD_NAME, encoding="utf-8")

        result = self.apply_series(patch)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            odd.read_text(encoding="utf-8"),
            "int odd(void) {\n    int added = 1;\n    return 0;\n}\n",
        )

        # The path must be recognised as series-owned, so a second run restores
        # it before applying the patch again.
        odd.write_text("int local(void) { return 9; }\n", encoding="utf-8")
        again = self.apply_series(patch)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual(
            odd.read_text(encoding="utf-8"),
            "int odd(void) {\n    int added = 1;\n    return 0;\n}\n",
        )


if __name__ == "__main__":
    unittest.main()
