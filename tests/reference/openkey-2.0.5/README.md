# OpenKey 2.0.5 — differential-test reference (unmodified upstream)

These files are the **unmodified** upstream OpenKey 2.0.5 engine sources
(https://github.com/tuyenvm/OpenKey, tag/version 2.0.5), copied verbatim to
serve as the third model in the differential correctness benchmark:

- `tests/mega_correctness.cpp` (differential suite) compares the NextGen
  `TextEngine` and the clean-room oracle (`tests/vi_oracle.hpp`) against this
  **actual** 2.0.5 engine implementation (compiled for Linux via its own
  `platforms/linux.h` path — no Windows API is required).

License: GPL-3.0 (see `LICENSE`). These files are NOT part of the OpenKey
NextGen codebase; they are a test fixture. Do not modify them — the benchmark
must always test the pristine 2.0.5 algorithm. Build quirks (e.g. ConvertTool
missing `<algorithm>`) are worked around in the build script, not by editing
these sources.
