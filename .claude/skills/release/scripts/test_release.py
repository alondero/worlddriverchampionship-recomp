"""Offline checks for release provenance and draft rejection; no GitHub writes."""
from contextlib import redirect_stdout
from copy import deepcopy
from io import StringIO
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import release


class ReleaseChecks(unittest.TestCase):
    def setUp(self):
        self.version = "v0.1.0"
        self.sha = "a" * 40
        self.run = {
            "path": release.WORKFLOW, "event": "workflow_dispatch",
            "head_sha": self.sha, "head_branch": self.version,
            "status": "completed", "conclusion": "success",
            "html_url": f"https://github.com/{release.REPO}/actions/runs/123",
        }
        self.published = {
            "id": 321, "tag_name": self.version, "draft": False, "prerelease": False,
            "html_url": f"https://github.com/{release.REPO}/releases/tag/{self.version}",
            "name": f"World Driver Championship Recompiled {self.version}", "body": "Reviewed notes.\n",
        }
        self.assets = [{"name": release.DEFAULT_ASSETS[0], "size": 100, "state": "uploaded"}]

    def verify(self, *, annotated=False, notes=None):
        def command(*args):
            if args[:3] == ("git", "rev-parse", "--verify"):
                return self.sha
            if args[:3] == ("git", "remote", "get-url"):
                return f"https://github.com/{release.REPO}.git"
            if args[:3] == ("git", "ls-remote", "--tags"):
                ref = f"refs/tags/{self.version}"
                return f"{'b' * 40}\t{ref}\n{self.sha}\t{ref}^{{}}" if annotated else f"{self.sha}\t{ref}"
            self.fail(f"unexpected command: {args}")

        def api(path):
            return deepcopy(self.run if path.startswith("actions/") else self.published)

        with patch.object(release, "command", side_effect=command), \
             patch.object(release, "api", side_effect=api), \
             patch.object(release, "pages", return_value=self.assets), redirect_stdout(StringIO()):
            release.verify(self.version, "123", release.DEFAULT_ASSETS, notes)

    def test_lightweight_and_annotated_tags_use_the_commit(self):
        self.verify()
        self.verify(annotated=True)

    def test_successful_run_for_another_source_is_rejected(self):
        for key, wrong in [("head_sha", "c" * 40), ("head_branch", "main"),
                           ("path", ".github/workflows/docs.yml"), ("event", "push")]:
            with self.subTest(key=key):
                previous = self.run[key]
                self.run[key] = wrong
                with self.assertRaises(ValueError):
                    self.verify()
                self.run[key] = previous

    def test_draft_prerelease_and_incomplete_workflow_are_rejected(self):
        for key in ("draft", "prerelease"):
            with self.subTest(key=key):
                self.published[key] = True
                with self.assertRaises(ValueError):
                    self.verify()
                self.published[key] = False
        self.run["status"] = "in_progress"
        with self.assertRaises(ValueError):
            self.verify()

    def test_missing_empty_and_incomplete_assets_are_rejected(self):
        for assets in ([], [{"name": release.DEFAULT_ASSETS[0], "size": 0, "state": "uploaded"}],
                       [{"name": release.DEFAULT_ASSETS[0], "size": 100, "state": "new"}]):
            with self.subTest(assets=assets):
                self.assets = assets
                with self.assertRaises(ValueError):
                    self.verify()

    def test_final_notes_must_match_reviewed_file(self):
        with tempfile.TemporaryDirectory() as directory:
            notes = Path(directory) / "notes.md"
            notes.write_text("Reviewed notes.\n", encoding="utf-8")
            self.verify(notes=notes)
            self.published["body"] = "Different notes."
            with self.assertRaises(ValueError):
                self.verify(notes=notes)

    def test_unfinished_notes_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            notes = Path(directory) / "notes.md"
            for text in (" \n", "Automated build from commit abc.", "Run <RUN_ID>.",
                         "Build <version>.", "See [Building][building].",
                         "See [Building][].", "\ufeffSome notes."):
                with self.subTest(text=text):
                    notes.write_text(text, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        release.check_notes(notes)
            notes.write_text("See [Building][BUILDING].\n\n[building]: https://example.com\n", encoding="utf-8")
            release.check_notes(notes)
            notes.write_text("See [Building][].\n\n[building]: https://example.com\n", encoding="utf-8")
            release.check_notes(notes)

    def test_first_release_includes_root_and_issue_closed_after_final_commit(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(release, "ROOT", Path(directory)):
            release.command("git", "init", "-b", "main")

            def make_commit(subject):
                release.command("git", "-c", "user.name=Release Checks",
                                "-c", "user.email=release-checks@example.invalid",
                                "commit", "--allow-empty", "-m", subject)
                return release.commit("HEAD")

            root = make_commit("root")
            head = make_commit("squashed fix")
            prs = [{"number": number, "merged_at": "2026-01-01T00:00:00Z",
                    "merge_commit_sha": sha, "html_url": f"https://example.com/pr/{number}",
                    "title": title} for number, sha, title in
                   [(1, root, "root-change"), (2, head, "included-change"),
                    (3, "d" * 40, "unrelated-change")]]
            issues = [{"number": 4, "title": "closed-after-commit", "html_url": "https://example.com/4",
                       "closed_at": "2099-01-01T00:00:00Z"}]
            with patch.object(release, "pages", side_effect=[prs, issues, prs, issues]):
                first, subsequent = StringIO(), StringIO()
                with redirect_stdout(first):
                    release.changes(None, "HEAD")
                with redirect_stdout(subsequent):
                    release.changes(root, "HEAD")
            self.assertIn("root-change", first.getvalue())
            self.assertNotIn("root-change", subsequent.getvalue())
            for output in (first.getvalue(), subsequent.getvalue()):
                self.assertIn("included-change", output)
                self.assertIn("closed-after-commit", output)
                self.assertNotIn("unrelated-change", output)


if __name__ == "__main__":
    unittest.main()
