#!/usr/bin/env python3
"""Fast path for gen_sha256sums.sh: hash Git blobs, never working-tree bytes."""

import hashlib
import os
import subprocess
import sys


def generate(manifest, prefix):
    paths = subprocess.check_output(["git", "ls-files", "-z"]).split(b"\0")
    # One Git process instead of several Git/MSYS processes per tracked file.
    # --batch returns a length-prefixed binary stream, including embedded NULs.
    with subprocess.Popen(
        ["git", "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE
    ) as git:
        def digest(revision):
            git.stdin.write(revision + b"\n")
            git.stdin.flush()
            header = git.stdout.readline()
            if header.endswith(b" missing\n"):
                return None
            fields = header.split()
            if len(fields) != 3 or fields[1] != b"blob":
                raise RuntimeError("Expected a Git blob: " + repr(header))
            remaining = int(fields[2])
            sha = hashlib.sha256()
            while remaining:
                chunk = git.stdout.read(min(remaining, 1024 * 1024))
                if not chunk:
                    raise RuntimeError("Truncated Git blob")
                sha.update(chunk)
                remaining -= len(chunk)
            if git.stdout.read(1) != b"\n":
                raise RuntimeError("Missing Git blob separator")
            return sha.hexdigest().encode("ascii")

        try:
            for path in sorted(paths):
                if not path or path == os.fsencode(manifest) or not os.path.isfile(path):
                    continue
                # The manifest is line-oriented; never silently truncate a path.
                if b"\n" in path or b"\r" in path:
                    raise RuntimeError("Unsupported newline in tracked path")
                sha = digest(b":" + path)
                if sha is None:
                    sha = digest(b"HEAD:" + path)
                    if sha is None:
                        raise RuntimeError("No index or HEAD blob for " + repr(path))
                    print("[sha256sums] NOTE: used HEAD for " + os.fsdecode(path),
                          file=sys.stderr)
                # Write bytes so Windows does not translate LF to CRLF.
                sys.stdout.buffer.write(
                    sha + b"  " + os.fsencode(prefix) + b"/"
                    + path.replace(b"\\", b"/") + b"\n"
                )
            git.stdin.close()
            if git.wait() != 0:
                raise RuntimeError("git cat-file failed")
        except BaseException:
            git.terminate()
            raise


if __name__ == "__main__":
    generate(sys.argv[1], sys.argv[2])
