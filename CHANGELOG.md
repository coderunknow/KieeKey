# Changelog

All notable changes to KieeKey are documented here. Format based on
Keep a Changelog; versioning: SemVer.

## [1.0.0] — 2026-08-31

### Project / branding
* Fork renamed to **KieeKey**; copyright held by **coderunknow**
  (https://github.com/coderunknow). KieeKey is a modified version based on
  [OpenKey](https://github.com/tuyenvm/OpenKey) (GPL-3.0) — the full upstream
  lineage and engineering history is preserved verbatim in `docs/reports/`
  (documents written under the pre-release working name "OpenKey NextGen",
  milestone numbers v3.0–v3.4; e.g. `V331_FIX_REPORT.md` is now
  `LINEAGE_v3.3.1_FIX_REPORT.md`).
* Project version unified to **1.0.0** (CMake project, .rc VERSIONINFO,
  tray/UI strings, singleton mutex, CI artifact names).
* Every fork-authored C++ source file now carries a GPLv3 copyright header
  retaining the upstream OpenKey copyright plus the KieeKey modification
  notice and the GPL "no warranty" statement
  (`SPDX-License-Identifier: GPL-3.0-or-later`).
* Added: root `LICENSE` (verbatim GNU GPLv3), `THIRD-PARTY-NOTICES.md`,
  `.gitignore` for C++/CMake/VS source releases.
* Vendored third-party reference sources under `tests/reference/`
  (OpenKey 2.0.5, UniKey) are kept **verbatim** under their original
  licenses for differential testing.

### Engine (inherited from the refactor lineage, finalized in v1.0)
* Port of the OpenKey 2.0.5 Telex/VNI engine to modern C++ as a per-instance
  state machine (`TextEngine`) with UTF-16 output and constexpr tables.
* Carried the upstream master fix for the tone-mark insertion bug
  (`_vowelForMark`): golden test `"as" -> "á"`.
* Lock-free Vyukov SPSC/MPMC queue between the low-level hook thread and the
  consumer thread; async, non-blocking keyboard/mouse hook pipeline.
* TSF text-store composer (no synthetic backspaces) and event-driven
  foreground process monitor with auto-exclusion.
* Flat generated phonetics tables for a cache-friendly hot path
  (`tools/gen_flat_tables.py` -> `src/core/FlatTables.hpp`).
* Deep E2E latency audit: event-driven ordering barrier, parked-flag wake
  protocol, zero-alloc TSF single-edit fast path (see
  `LATENCY_AUDIT_REPORT.md`).

### Packaging
* CI workflow hardened: CI performs NO NuGet source registration at all
  (`nuget sources add` for a source name the runner image already registers
  fails with "The name specified has already been added to the list of
  available package sources") and NO Developer Command Prompt step — the
  CMake Visual Studio generator selects the per-platform toolset itself,
  which re-enables **ARM64EC** in the matrix via `-A ARM64EC`
  (vcvarsall.bat has no `arm64ec` argument and cannot set it up). Unit
  tests (ctest) run only on the native-arch x64 job: x64 Windows cannot
  execute ARM64/ARM64EC binaries. CI builds the dependency-free default
  configuration (`KIEEKEY_BUILD_UI=none`) so
  `find_package(Microsoft.WindowsAppSDK)` cannot break configure;
  `actions/checkout` bumped to v5.
* ARM64/ARM64EC portability + `/W4 /WX` hygiene: the spin-wait paths use
  the new shared `src/core/CpuPause.hpp` (PAUSE on x86/x64/ARM64EC, YIELD
  on ARM64/ARM) instead of unconditional `<immintrin.h>`/`<emmintrin.h>`
  includes (x86-family-only headers that hard-error C1189 on ARM64); the
  intentional cache-line padding warning is disabled (`/wd4324`);
  test `CHECK` macros route the condition through a non-constexpr bool
  helper (avoids MSVC C4127 on constant conds *and* C2131 from
  `if constexpr` on runtime conds); `FlatMap::find` takes the key by
  template and `static_cast`s to the stored key type (C4244
  `char32_t`→`uint16_t`); the foreground-HWND diagnostic stamp is
  explicitly truncated to its 32 significant bits (C4244). Added an
  `arm64ec-release` CMake preset mirroring the CI matrix.
* Renamed remaining lineage identifiers for consistency:
  `OPENKEY_PROFILE` → `KIEEKEY_PROFILE`, target `openkey_winui3` →
  `kieekey_winui3`.
* Source-only release: prebuilt binaries are no longer shipped in the
  repository (build via CMake presets or the included GitHub Actions
  workflow — x64 + ARM64; `bin/README.txt` explains how to obtain
  binaries). Regenerable frozen benchmark evidence (`imebench_kit/results*`,
  `benchmark_runs/`, ≈68 MB) is not shipped; reports in `docs/reports/`
  summarize the runs and the harness reproduces them.

### Versioning note

Code comments and CMake targets may reference internal lineage milestones
(`v3.0`–`v3.4`, phase codes `S1`–`S3`, `D2`–`D4`). These denote milestones
of the engineering lineage that produced KieeKey v1.0 (the pre-release
working name was "OpenKey NextGen") — they are historical engineering
annotations, not release versions. The first public release is **v1.0**.
