bin/ — build output placeholder
===============================

This directory is reserved for locally built binaries and is intentionally
EMPTY in the source release: .gitignore ignores everything under bin/
except this README, so anything you drop here stays out of git.

How to get binaries:
  1. Build locally:
       cmake --preset x64-release
       cmake --build --preset x64-release
     -> the executable is written to out/x64-release/ (not bin/).

  2. Or download a CI build from the GitHub Releases page of
     https://github.com/coderunknow/KieeKey (the Actions workflow in
     .github/workflows/build.yml builds x64 / ARM64 / ARM64EC on every tag).
