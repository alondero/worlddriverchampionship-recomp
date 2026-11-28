import contextlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from scripts import check_docs


class CheckDocsTest(unittest.TestCase):
    def test_checks_tracked_and_new_docs_while_skipping_ignored_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet"], cwd=root, check=True)
            (root / ".gitignore").write_text("artifacts/\n", encoding="utf-8")
            (root / "artifacts").mkdir()
            (root / "artifacts/draft.md").write_text("[bad](missing.md)\n", encoding="utf-8")
            (root / "Tracked.md").write_text("[draft](New%20Draft.md)\n", encoding="utf-8")
            (root / "New Draft.md").write_text("# New draft\n", encoding="utf-8")
            subprocess.run(["git", "add", "--", "Tracked.md"], cwd=root, check=True)

            output = io.StringIO()
            with patch.object(check_docs, "ROOT", root), patch.object(check_docs, "REQUIRED", ["Tracked.md"]):
                with contextlib.redirect_stdout(output):
                    self.assertEqual(check_docs.main(), 0)
                self.assertIn("2 Markdown files", output.getvalue())
                (root / "New Draft.md").write_text("[bad](missing.md)\n", encoding="utf-8")
                errors = io.StringIO()
                with contextlib.redirect_stderr(errors):
                    self.assertEqual(check_docs.main(), 1)
                self.assertIn("broken link: New Draft.md", errors.getvalue())
