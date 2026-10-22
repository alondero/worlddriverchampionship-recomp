"""Exercise the publication guard against a real Git index."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RepositoryHygieneTests(unittest.TestCase):
    def test_local_artifacts_are_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            shutil.copyfile(ROOT / ".gitignore", root / ".gitignore")
            paths = [
                ".env", ".env.production", "credentials.pem", "signing.key",
                ".worktrees/session/source.cpp", "rom-assets/dump.dat",
                "capture.png", "capture.mp4", "release.zip", "player.exe",
                "World Driver Championship (USA).z64", "RecompiledFuncs/func.c",
                "src/aspMain.cpp", "CLAUDE.local.md",
            ]
            result = subprocess.run(
                ["git", "-C", str(root), "check-ignore", "-z", "--stdin"],
                input=("\0".join(paths) + "\0").encode(), capture_output=True,
            )
            self.assertEqual(set(result.stdout.decode().rstrip("\0").split("\0")), set(paths))

    def test_forced_add_cannot_bypass_publication_guard(self):
        spec = importlib.util.spec_from_file_location("check_repo", ROOT / "scripts/check_repo.py")
        guard = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(guard)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / ".gitignore").write_text("*.z64\n.env\n", encoding="utf-8")
            (root / "source.cpp").write_text("// host source\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            self.assertEqual(guard.check(root), [])
            (root / "local dump.z64").write_bytes(b"synthetic fixture")
            (root / ".env").write_text("EXAMPLE=fixture\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(root), "add", "-f", ".env", "local dump.z64"], check=True)
            self.assertEqual(guard.check(root), [".env", "local dump.z64"])


if __name__ == "__main__":
    unittest.main()
