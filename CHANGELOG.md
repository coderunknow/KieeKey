# Changelog

All notable changes to KieeKey are documented here. Format based on
Keep a Changelog; versioning: SemVer.

## [1.1.0] — 2026-09-01

A UX- and correctness-focused release: the tray app becomes properly
discoverable and HiDPI-correct, two latent engine/transport defects found by
UBSan and by a randomized differential harness are fixed, and the edge-case
test surface grows to cover them.

### Added
* **Tray app** — a plain left click on the tray icon now opens the status
  menu (previously it did nothing at all, which read as a dead icon; only
  double-click was wired up).
* **Tray menu** — two new items: **Khởi động cùng Windows** (writes/removes
  the `HKCU\…\Run` entry, and is reconciled with the real Run key at
  startup so it can never disagree with Task Manager) and **Giới thiệu
  KieeKey…**.
* **Toggle confirmation** — optional balloon on every Ctrl+Shift toggle
  (default **on**; the tray icon alone is easy to miss on a dark taskbar, and
  "did it switch?" is the most common confusion). Also exposed in Cài đặt →
  Bàn phím.
* **Settings dialog** — **Khôi phục mặc định** restores the shipped defaults
  (deliberately leaving the startup entry alone — it is the one setting with
  an effect outside KieeKey), plus a version footer.
* **Diagnostics** — two new rows in Cài đặt → Chẩn đoán:
  *Lần nghẽn hàng đợi* and *Nghẽn lâu nhất*, fed by the new queue-saturation
  watchdog.
* **Tests** — two new ctest targets: `ok_chord_tests`
  (`tests/test_hotkey_chord.cpp`, the Ctrl+Shift chord state machine) and
  `ok_edge_tests` (`tests/test_engine_edge_cases.cpp`, engine boundaries).
  The native suite is now **5/5**.

### Changed
* **HiDPI settings dialog.** Every coordinate now lives on a 96-dpi design
  grid that is scaled through `S()` at creation and again on
  `WM_DPICHANGED`, and the frame is sized and centred in `WM_CREATE` once the
  real monitor DPI is known. Previously the layout was hard-coded 96-dpi
  pixels under a PerMonitorV2 manifest, so at 150 %/200 % the OS scaled the
  frame but nothing inside it — a postage-stamp window with clipped
  Vietnamese text.
* **Tray toggle wording.** The toggle item now says what the click *will do*
  ("Tắt gõ tiếng Việt" while the IME is on) and carries a checkmark for the
  current state, instead of a permanently-labelled "Bật" item.
* **Diagnostics plumbing** — `Win32Wrapper` forwards the new
  `saturationRuns()` / `peakSaturationUs()` accessors.
* Version bumped to **1.1.0** everywhere: CMake `project()`, `.rc`
  VERSIONINFO (`FILEVERSION`/`PRODUCTVERSION 1,1,0,0`, strings `1.1.0.0`),
  the manifest `assemblyIdentity`, `KieeKey v1.1.0` titles, file banners,
  demo console title/output, README. Historical documents under
  `docs/reports/` and the 1.0.2/1.0.0 changelog sections are intentionally
  left verbatim (lineage record).

### Fixed
* **Core — out-of-bounds read on the first keystroke of a word.** UBSan
  flagged `checkForStandaloneChar()` reading `buffer[index_ - 1]` with
  `index_ == 0` (size_t wraparound): `… index 18446744073709551615 out of
  bounds for 'unsigned int [32]'`. Guarded with `index_ > 0`. The guard is
  behaviour-preserving (with `index_ == 0` the garbage read could never equal
  `keyWillReverse`, so control reached the `index_ == 0` branch anyway) —
  verified against 240k randomized streams plus the 149 curated cases.
* **Core — `ProcessMonitor` no longer uses `std::atomic<std::shared_ptr<T>>`,**
  which is deprecated in C++20, *removed* in C++26 and already gone from
  libc++ (hard `static_assert`). Replaced with a mutex-guarded `shared_ptr`
  published through `publishSnapshot()`. Snapshot traffic happens on
  foreground changes only, never on the per-key hot path, so this is
  cost-neutral and unblocks clang/libc++ toolchains.
* **Transport — the queue-saturation budget is now live.**
  `kMaxAcceptableHookLatencyNs` was declared but never consulted; a wedged
  consumer only nudged the "dropped" counter. A queue that stays full past
  200 ms is now recorded as a distinct saturation run with a peak duration.
* **App — KieeKey no longer processes keys typed into KieeKey.** The
  exclusion cache now also excludes our own windows by PID. An IME that eats
  the keys you type into its own settings dialog is unusable, and any future
  text field would have been corrupted.
* **App — the Ctrl+Shift chord state is reset on every foreground change.**
  A key-up delivered to *another* window (Alt+Tab away, a UAC prompt, an RDP
  grab) never reaches the hook, so a stuck Ctrl/Shift made the next bare
  Shift press look like a completed chord and silently switched the IME off.
* **App — the singleton names no longer embed the version.** They used to be
  `KieeKey_1.0.2_Singleton`, so an upgraded build could not see a live 1.0.2
  and installed a **second** low-level keyboard hook over the same
  keystrokes (doubled or swallowed characters). The names are now stable, and
  the legacy names are still *probed* so an in-place upgrade is reported
  ("a KieeKey cũ đang chạy") instead of double-hooking.
* **App — word-break coverage.** `Ctrl+Break` (VK_CANCEL), the
  context-menu key (VK_APPS) and VK_SLEEP were missing from the word-break VK
  set, so a pending composition could survive them.
* **App — the classic Win32 tray-menu bug.** A `WM_NULL` round-trip is now
  posted after `TrackPopupMenu`, so the popup always finishes its modal loop;
  without it the menu can stick open and swallow the next click.
* **App** — removed the dead `kMaxInlineInputs` constant (the inline emitter
  owns that budget now).

## [1.0.2] — 2026-08-31

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
* Version bumped to **1.0.2** everywhere it appears: CMake `project()`,
  `.rc` VERSIONINFO (`FILEVERSION`/`PRODUCTVERSION 1,0,2,0`, version
  strings `1.0.2.0`), first-run balloon + settings-dialog title, singleton
  mutex (`KieeKey_1.0.2_Singleton`), file banners, demo console title and
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

## Release checklist

The version string appears in five places; all of them must agree before a
tag is pushed (`grep -rn "1\.1\.0" --include=*.{txt,md,hpp,cpp,rc,manifest} .`
is a quick sanity check):

1. `CMakeLists.txt` — `project(KieeKey VERSION …)`
2. `src/app/KieeKeyApp.rc` — `FILEVERSION`, `PRODUCTVERSION`, `FileVersion`,
   `ProductVersion`
3. `src/app/KieeKeyApp.manifest` — `<assemblyIdentity version="…">`
4. `src/app/main.cpp` — `kAppVersion[]`
5. `README.md` / `CHANGELOG.md` / file banners / `demo/main.cpp`

Then, as the LAST step before tagging (the manifest covers every tracked file
except itself, so it must be regenerated after all other edits):

```sh
./tools/make_sha256sums.sh && git add SHA256SUMS.txt
```

Consumers verify a download with `sha256sum -c SHA256SUMS.txt`.
