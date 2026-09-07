bin/ — build output placeholder
===============================

This directory is reserved for locally built binaries and is intentionally
EMPTY in the source release: .gitignore ignores everything under bin/
except this README, so anything you drop here stays out of git.

How to get binaries:
  1. Build locally (the presets default to KIEEKEY_BUILD_UI=none — the
     dependency-free Win32 tray app, no Windows App SDK needed):
       cmake --preset x64-release
       cmake --build --preset x64-release
     -> the executable is written to out/x64-release/ (not bin/).
     (v1.2.2 RC4 note: the presets previously forced KIEEKEY_BUILD_UI=winui3,
     which made this first step fail on machines without the Windows App SDK;
     pass -DKIEEKEY_BUILD_UI=winui3 explicitly if you want the WinUI 3
     front-end and have the SDK installed.)

  2. Or download a CI build from the GitHub Releases page of
     https://github.com/coderunknow/KieeKey (the Actions workflow in
     .github/workflows/build.yml builds x64 / ARM64 / ARM64EC on every tag).
