# Changelog

All notable changes to KieeKey are documented here. Format based on
Keep a Changelog; versioning: SemVer.

## [1.0.1] — 2026-08-31

### Changed
* Official logo refresh: the color "kie" keycap mark (uploaded 500×500 alpha
  PNG) is now the application icon (`KieeKeyApp.ico` — green/ON state) and
  the README hero image (`KieeKeyApp-preview.png` — a byte-identical copy of
  the uploaded artwork, SHA-256-verified); the monochrome keycap mark
  becomes the gray/OFF state icon (`KieeKeyApp-off.ico`). Both `.ico` files
  embed NATIVE 16/24/32/48/64/128/256 px frames — the previous files carried
  a single 256 px frame, which Windows downscaled to a blurry tray icon.
  The ON icon's frames are exact LANCZOS resizes of the uploaded composition
  (classic 32-bit BMP frames plus a PNG-compressed 256 px frame, the
  maximally compatible layout for Explorer/shortcuts/taskbar); the OFF icon
  artwork is alpha-trimmed and centered with a small margin so its keycaps
  fill the canvas at tray size.
* Version bumped to **1.0.1** everywhere it appears: CMake `project()`,
  `.rc` VERSIONINFO (`FILEVERSION`/`PRODUCTVERSION 1,0,1,0`, version
  strings `1.0.1.0`), first-run balloon + settings-dialog title, singleton
  mutex (`KieeKey_1.0.1_Singleton`), file banners, demo console title and
  output, README. Historical documents under `docs/reports/` and the 1.0.0
  changelog section above are intentionally left verbatim (lineage record).

### Fixed
* **Reliability — "KieeKey suddenly stops and cannot be opened again"** (all
  four root causes closed):
  * Bounded shutdown: `ModernKeyHook::stop()` and `ProcessMonitor::stop()`
    now wait at most 3 s / 2 s and **detach** a stuck worker instead of
    joining forever. Previously, if the consumer thread was wedged inside a
    synchronous TSF edit session (`RequestEditSession` `TF_ES_SYNC`
    marshals into the focused app's STA — a hung target never returns),
    exit hung the UI thread and the process survived as a zombie **holding
    the single-instance mutex**, so every relaunch reported "KieeKey đang
    chạy" until a reboot. Both pumps also arm a 1 s fallback timer so a
    lost `WM_QUIT` can never stall shutdown.
  * Second-instance takeover: a relaunch now pings the earlier instance
    (`SMTO_ABORTIFHUNG`). A healthy instance is asked (named wake event) to
    restore its tray icon and confirm with a balloon; a **hung** one is
    terminated (registry PID validated against our own image name) and the
    new process takes over the singleton — no reboot / Task Manager needed.
  * Tray-icon resurrection: `TaskbarCreated` is handled, so an Explorer
    restart no longer leaves KieeKey running invisibly ("suddenly stopped").
  * Foreground-hang gate: after an app switch, TSF output is enabled only
    after the UI thread's bounded `WM_NULL` probe (150 ms,
    `SMTO_ABORTIFHUNG`) proves the new foreground is pumping; while gated —
    or when the probe fails — edits use inline SendInput, which cannot
    wedge the pipeline. The consumer also skips TSF focus re-resolution
    while gated. Recovery is automatic when a temporarily hung app resumes.
* (carried from the post-1.0 CI hardening) MSVC duplicate-manifest link
  failure (CVT1100/LNK1123) — `/MANIFEST:NO`; deterministic
  `EditDrainBarrier` tests (exact-iteration hook injection + handshakes
  replace scheduler-dependent sleeps); Win32 `timeouts()` diagnostics
  counter now actually counts; `timeBeginPeriod(1)` makes the documented
  1 ms hook-thread wait budget real; ctest `TIMEOUT 120` hang protection.

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

### Fixed
* MSVC/CI link failure — `CVTRES : fatal error CVT1100: duplicate resource.
  type:MANIFEST, name:1, language:0x0409` + cascade `LNK1123: failure during
  conversion to COFF: file invalid or corrupt` on all three matrix jobs
  (x64/ARM64/ARM64EC). Root cause: under the Visual Studio generator, MSBuild
  exe targets default to `GenerateManifest` + `EmbedManifest`
  (`/MANIFEST:EMBED`), so link.exe auto-embedded its own `RT_MANIFEST` ID 1
  next to the identical resource compiled from `KieeKeyApp.rc` — two
  application manifests in one binary. Fix: `/MANIFEST:NO` on the openkey
  MSVC link line; the .rc remains the single manifest source on every
  toolchain (MinGW/windres never auto-embeds, which is why local cross
  builds passed while MSVC CI failed). A configure-time guard now fails CMake
  if a `.manifest` file is ever listed as a target source (MSBuild would
  embed it as an Additional Manifest File a second time).

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
  constant-condition test assertions use `if constexpr` (C4127); the
  foreground-HWND diagnostic stamp is explicitly truncated to its 32
  significant bits (C4244). Added an `arm64ec-release` CMake preset
  mirroring the CI matrix.
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
