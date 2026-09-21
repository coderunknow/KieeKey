#!/usr/bin/env python3
"""Integration tests for the manifest fast path using disposable Git indexes."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)
        self.env = os.environ.copy()
        for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE"):
            self.env.pop(key, None)
        self.git("init", "-q")
        (self.repo / "scripts").mkdir()
        for name in ("gen_sha256sums.sh", "gen_sha256sums.py"):
            shutil.copyfile(ROOT / "scripts" / name, self.repo / "scripts" / name)
        self.files = {
            ".gitattributes": b"*.txt text eol=lf\n",
            "text.txt": b"staged\r\ntext\r\n",
            "binary.bin": bytes(range(256)) * 8193,
            "empty": b"",
            "space and tiếng Việt.txt": b"unicode path\n",
        }
        for name, content in self.files.items():
            (self.repo / name).write_bytes(content)
        self.git("add", "--", *self.files)

    def git(self, *args, check=True):
        return subprocess.run(["git", *args], cwd=self.repo, env=self.env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              check=check).stdout

    def script(self, *args):
        return subprocess.run(["bash", "scripts/gen_sha256sums.sh", *args],
                              cwd=self.repo, env=self.env, capture_output=True)

    def expected(self):
        return b"".join(
            hashlib.sha256(self.git("show", ":" + name)).hexdigest().encode()
            + b"  KieeKey/" + name.encode() + b"\n"
            for name in sorted(self.files, key=lambda name: name.encode())
            if (self.repo / name).is_file()
        )

    def test_binary_paths_normalization_and_staged_content(self):
        (self.repo / "text.txt").write_bytes(b"UNSTAGED replacement\n")
        (self.repo / "untracked.txt").write_bytes(b"not in manifest\n")
        result = self.script()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.repo / "SHA256SUMS.txt"
        self.assertEqual(manifest.read_bytes(), self.expected())
        self.git("add", "SHA256SUMS.txt")
        self.assertEqual(self.script("--check").returncode, 0)
        # A stale manifest must fail; CRLF in the manifest itself is tolerated.
        manifest.write_bytes(manifest.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(self.script("--check").returncode, 0)
        self.git("add", "text.txt")
        self.assertNotEqual(self.script("--check").returncode, 0)

    def test_missing_worktree_file_matches_legacy_skip(self):
        (self.repo / "empty").unlink()
        self.assertEqual(self.script().returncode, 0)
        self.assertEqual((self.repo / "SHA256SUMS.txt").read_bytes(), self.expected())

    def test_head_fallback_for_unmerged_index(self):
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "fixture", "--no-verify")
        oid = self.git("rev-parse", "HEAD:text.txt").strip()
        self.git("update-index", "--force-remove", "text.txt")
        # A single non-stage-zero entry makes :text.txt unavailable, as in a conflict.
        subprocess.run(["git", "update-index", "--index-info"], cwd=self.repo,
                       env=self.env, input=b"100644 " + oid + b" 2\ttext.txt\n", check=True)
        result = self.script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"used HEAD", result.stderr)
        self.assertIn(hashlib.sha256(self.git("show", "HEAD:text.txt")).hexdigest().encode(),
                      (self.repo / "SHA256SUMS.txt").read_bytes())

    def test_missing_blob_fails_without_replacing_manifest(self):
        original = b"must not replace on error\n"
        (self.repo / "SHA256SUMS.txt").write_bytes(original)
        self.git("update-index", "--cacheinfo", "100644", "1" * 40, "empty")
        result = self.script()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.repo / "SHA256SUMS.txt").read_bytes(), original)

    @unittest.skipIf(os.name == "nt", "PATH-only fallback fixture uses POSIX symlinks")
    def test_without_python_matches_fast_path(self):
        self.assertEqual(self.script().returncode, 0)
        fast = (self.repo / "SHA256SUMS.txt").read_bytes()
        # Expose the original shell dependencies but no Python executable.
        bindir = self.repo / "shell-only"
        bindir.mkdir()
        for tool in ("bash", "dirname", "git", "tr", "grep", "sort", "sha256sum",
                     "cut", "mv", "wc", "diff", "head"):
            (bindir / tool).symlink_to(shutil.which(tool))
        self.env["PATH"] = str(bindir)
        result = self.script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.repo / "SHA256SUMS.txt").read_bytes(), fast)
        self.assertEqual(self.script("--check").returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
