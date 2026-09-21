# KieeKey
## Project Status

**KieeKey is currently under active development!**

Development has resumed after a period of being frozen due to the project's growing bug count and maintenance complexity.

The current Stable release remains available for anyone who wants to use or experiment with it. New development is focused on improving reliability, fixing known issues, and carefully introducing changes without expanding the scope unnecessarily.

New features and major changes may be introduced during development, but they will be tested before being considered for a Stable release.

At this stage, development builds should be considered **pre-release** and may contain bugs or incomplete changes.

The project may still be paused again in the future if development no longer provides enough value to justify the maintenance cost.


[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Language](https://img.shields.io/badge/language-C%2B%2B20%2FC%2B%2B23-00599C.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-0078D6.svg)
![Build](https://img.shields.io/badge/build-CMake%20%3E%3D%203.28-064FAD.svg)

**KieeKey v1.3.0-beta7** is a modern, low-latency Vietnamese input method
engine (bộ gõ Tiếng Việt) for Windows, with a system-tray application, a TSF
text-store composer and an optional WinUI 3 Fluent settings UI.

> **KieeKey is a modified version based on
> [OpenKey](https://github.com/tuyenvm/OpenKey)** by Tuyen Mai (GPL-3.0).
> The original C++11/Win32 engine of OpenKey 2.0.5 was fully ported to
> modern C++, refactored and hardened. KieeKey inherits the **GNU General
> Public License v3.0** in its entirety and keeps upstream attribution in
> every source file.

![KieeKey preview](src/app/KieeKeyApp-preview.png)

---

## What's new in v1.3.0-beta7 — Diagnostics & snapshot truth fix

Beta7 is the diagnostics-truth release (Windows file version **1.3.0.8**): the beta6
report was trusted as fact and found to contradict itself — `dpi: 96` in the snapshot
vs `display-metrics dpi=144`, `SendInputCalls: 0` vs 13 deliveries in the emit-chain
trace, and every runtime field (uptime, workingSet, os, arch, foreground) stuck at
`0`/empty. Root cause: `SystemSnapshot` was never refreshed (only the B1/B4 evidence
blocks were), the inline `SendInput` path never incremented `SendInputCalls`, and hook
counters (`pushed`/`dropped`/`wakes`/`SetEvent`) lived only in the wrapper. This release
fixes all three at the source, labels the provenance (`dpi (snapshot, monitor): 96
(default = not refreshed / should match display-metrics …)` and `uptime: 0 s (0 = snapshot
not refreshed; should be >0 when keyboard events >0)`), syncs live counters into
`Diagnostics::set()` before every export/copy/quick-check and on startup, and adds a
portable regression `tests/test_diagnostics_beta7_repro.cpp`. See `docs/release-notes-v1.3.0-beta7.md`.

* **Snapshot fully refreshed**: `refreshSystemSnapshot()` now fills OS, arch, app version,
  uptime, workingSet/peak, kernel/user CPU, foreground app, keyboard layout, output/input
  mode, codeTable, DPI and flags from live Win32 probes on every report/export/copy.
* **Counters synced**: `SendInputCalls` increments on the hot inline path; `syncDiagnosticsCounters()`
  mirrors `pushed`/`dropped`/`consumerWakes`/`SetEventSyscalls` and keyboard totals from the
  live `HookCounters` into the report before export.
* **Labels honest**: DPI and uptime rows now state when `96`/`0` means "not refreshed" and what
  they should match, so a future mismatch is a clue, not a silent lie.

## What's new in v1.3.0-beta6 — Self-audit hardening (preparing v1.3.0-rc1)

Beta6 is the self-audit release (Windows file version **1.3.0.7**): there was
no new tester report — KieeKey audited itself. Every finding below is labelled
`[VERIFIED]` (reproduced by a probe/test before fixing) or `[HYPOTHESIS]`
(observed in source, unproven) in `docs/release-notes-v1.3.0-beta6.md`.

* **CI gates closed**: the dialog layout audit (`--strict`) and the
  dialog-controls audit now run in the test suite — they used to pass only
  because a human ran them by hand. Both were proven to fail on seeded
  violations before being trusted as gates.
* **Web player parity**: the per-game steering choice (B7) reached the HTML5
  player; the B5 red divergent tail + "Backspace để sửa" hint is now actually
  VISIBLE there (the web HUD used to show the status line instead of the
  hint); the GateBlocker readout is documented as intentionally win32-only.
* **Chaos Lab hardened**: every one of its 17 controls audited end-to-end
  (created → consumed → engine → persisted); the intensity slider's
  percent rounding fixed; intensity / glyph mode / granularity now survive a
  restart; a new source-contract audit guards the window in the suite.
* **Tester-evidence diagnostics**: the exported/copied diagnostics report now
  carries machine-readable proof lines — the last 32 output deliveries with
  target window class/process and the live-effects gate verdict at emit time,
  which Win32 API resolved the foreground process name, and the real
  DPI/font/DWM metrics — plus a one-click "copy report to clipboard" button.
* **Benchmarks on CI**: the full no-regression A/B campaign (feature
  isolation medians + the e2e shim pipeline) now runs as a GitHub Actions
  workflow on a quieter runner, closing the beta5-owed e2e re-check.

## What's new in v1.3.0-beta5 — Nine reported bug clusters fixed at the root

Beta5 is the tester-report release (Windows file version **1.3.0.6**). Every
one of the nine bug clusters from the beta4 Windows test report was
reproduced, root-caused, fixed, and pinned by portable tests; the honest
per-bug dossier (root cause → evidence → fix → test, with the Windows-only
residuals called out) lives in `docs/release-notes-v1.3.0-beta5.md`.

* **B1 — Settings dialog overlap/clip, no scroll.** The runtime layout solver
  now reflows and rescales every one of the nine tabs against the real DPI
  metrics and the actual work-area height, with page scrolling where content
  still does not fit; pinned by `tests/test_dialog_layout.cpp`.
* **B2 — Live effects did not reach external apps.** The full producer→ring→
  consumer output chain is now proven lossless by a portable full-chain test
  (2 400 styled edits, zero loss, byte-identical output), and the dialog shows
  a LIVE GATE readout naming the exact condition that vetoes effects (IME
  off, excluded foreground app, non-Unicode code table…) in the tab-6 footer,
  the tray tooltip and the diagnostics quick-check.
* **B3 — "Sự kiện bàn phím đã xử lý" climbed on mouse drag.** The row was fed
  by the shared ring counter that keyboard, mouse and foreground events all
  increment; every tab-3 row now displays its own per-source counter, pinned
  by a source-contract audit plus the display-row partition test.
* **B4 — Chẩn đoán mostly zeros + "Ứng dụng hiện tại: unknown".** Process
  identity now resolves through a four-step fallback chain (query → module
  path → AUMID → window title) and the diagnostics recorders were wired to
  the counters they display.
* **B5 — Typing games: backspace-after-a-wrong-word "didn't work".** It did —
  it was invisible. All four typing games now render the composed buffer with
  the divergent tail in red plus the exact repair hint ("Sai — nhấn Backspace
  N lần để sửa"), pinned by frame-level assertions and a 1 000-seed recovery
  fuzz.
* **B6 — No-Mistake: a wrong FINAL word never ended the run; WPM/accuracy
  stayed 0; the apply button did not persist.** An end-of-run verdict now
  judges the final word through the same penalty ladder (one-shot; a
  tone-pending tail still waits for its repair key), the HUD shows live
  WPM/accuracy, "Áp dụng cấu hình game" persists to the registry (and is
  restored at boot), and the manager's config lock order was fixed.
* **B7 — WASD Race wanted WASD steering in Vietnamese mode.** New per-game
  "Phím lái" choice: *Mũi tên* (default), *WASD — lái VÀ gõ Telex/VNI*, or
  *Cả hai*. In the WASD-involving modes a/s/d/w steer the car AND feed the
  composer — W is never swallowed — persisted and live-applied, pinned by a
  1 000-seed steering fuzz across all three modes.
* **B8 — Chaos Lab was undiscoverable.** A 🧪 Chaos Lab row now sits under the
  eight games in the Arcade Hub sidebar, the README documents every entry
  point, and each of them reports a failed open with its Win32 error code
  instead of silently doing nothing (same for the Arcade Hub itself).
* **B9 — "Bàn phím" tab options "didn't work".** A systematic three-layer
  audit (`scripts/audit_settings_wiring.py`: 37 interactive controls ×
  created / read / reflected / live-apply / persisted / consumed) found the
  real breaks: tab-0 and tab-1 controls only applied via OK/Apply (toggling
  one and closing with X silently discarded it) and the Live-effects / Chaos
  / AI-opt-in toggles applied on click but were never persisted, so every
  restart reverted them. Every interactive control now applies on click and
  persists, and `tests/test_settings_wiring.cpp` pins probe-verified engine
  output deltas for every tab-0 option (most are conditional by design —
  digits only matter in VNI, quickTelex only on doubled consonants, etc.).
* **G1 — performance transparency.** New `docs/PERFORMANCE.md` publishes the
  measured hot-path cost of every opt-in feature (noise-floor-controlled
  medians), and each toggle's dialog text now carries its own measured cost.

The committed `dist/KieeKeyApp.exe` convenience build is refreshed to this
release (PE **1.3.0.6**) — building it compiles every Windows translation
unit and links the real PE32+ executable. The no-regression benchmark
campaign against the beta4 tag (paired, interleaved, byte-identical
noise-control leg) is documented in `docs/bench/beta5/BENCHMARK_REPORT.md`.

## What's new in v1.3.0-beta4 — Six reported defects fixed at the root

Beta4 is a correctness + completeness release (Windows file version **1.3.0.5**).
Every fix below was reproduced first, then fixed, then pinned by a native
regression test (`tests/test_arcade_recovery.cpp` and friends — no Windows
required to prove the logic):

* **Backspace no longer wedges a game (the reported "sai rồi backspace thì
  không gõ tiếp được").** The in-window composer mirrored the IME's
  raw-key/visible-buffer split, so after a wrong tone key one Backspace left the
  engine composing against invisible state forever (500/500 seeded fuzz runs
  failed before the fix). `VnComposer::feedBackspace()` now pops one *composed*
  code point — exactly what the screen shows — and returns the engine to a fresh
  word state, so its raw-key history can never disagree with the visible text.
  Proven deterministically and by a 300-seed recovery fuzz that must complete
  every run.
* **WasdRace ignored Backspace.** Real front-ends deliver it as `vk=0x08, ch=0`;
  the game only matched `ch == '\b'`. Both shapes now rewind, in VN and EN mode.
* **Every typing arcade speaks Vietnamese now.** Fishing and No-Mistake were
  deliberately ASCII-only; both gained `setPassageLanguage()` with full-diacritic
  prompts/streams composed through `VnComposer` (No-Mistake judges *words*, not
  raw keys — mid-syllable Telex is not a mistake; Backspace repairs a syllable
  before it is committed). Rhythm's lane keys moved to the **arrow cluster** in
  VN mode (d/f/j/k stay as aliases) and its notes show Vietnamese syllables.
* **A real concurrency race in the Arcade manager is closed.** Two front-end
  threads (hub timer vs web bridge tick) could run `update()`/`handleKey()` on
  the same game simultaneously — the concurrency test segfaulted ~15% of runs on
  unmodified main. All game calls are now serialized by a dedicated
  `m_gameMtx` (the IME hot path still takes no lock); TSAN-clean, 300-run
  hammer passes.
* **The settings dialog cannot clip text anymore.** The authored rectangles were
  re-fit (three clipped labels, one out-of-page group, two near-page controls,
  and the nine squeezed tab headers — "Phòng Chaos"/"AI Rival"/"Tiến trình"
  were unreadable) and — closing beta3's tracked follow-up — the beta3 layout
  solver (`DialogLayout.hpp`) is now WIRED INTO `WM_CREATE`: labels are measured
  with `DrawTextW(DT_CALCRECT)` on the real font/monitor and reflowed
  (grow → push down → stretch group boxes → grow the window) at any DPI.
* **Live external effects visibly transform Vietnamese.** The glyph tables had
  no mappings for precomposed Vietnamese vowels, so typing Vietnamese showed no
  change at any intensity — the reported "mode không hoạt động khi gõ ở ngoài".
  All accented vowels now mirror by tone (sắc↔huyền, hỏi↔ngã, nặng→hỏi),
  1:1 with the input so the erase accounting stays exact. The intensity selector
  gains 75%, and the hint now states the Unicode-table requirement up front.
* **The diagnostics module is actually used.** `ok::diag` shipped in beta3 with
  zero call sites. The Chẩn đoán tab now has the missing control panel —
  Off/Basic/Full level (persisted), a real quick self-check, and report export
  to `%APPDATA%\KieeKey` — and the hot path records keyboard counters,
  hook→decision, engine-decision, TSF-commit and SendInput latencies, all gated
  to one relaxed load when Off.

The committed `dist/KieeKeyApp.exe` convenience build is refreshed to this
release (PE **1.3.0.5**) — building it compiles every Windows translation unit
and caught three Windows-only compile errors in the new diagnostics code before
tagging. The no-regression benchmark campaign against the beta3 tag (paired,
interleaved, with a byte-identical noise-control leg) is documented in
`docs/bench/beta4/BENCHMARK_REPORT.md`.

## What's new in v1.3.0-beta3 — Five reported defects fixed at the root

Beta3 is a correctness release (Windows file version **1.3.0.4**) that fixes five
user-reported defects, each at its root cause and each covered by a native
regression test that runs on Linux (no Windows required to prove the logic):

* **Typing games now speak Vietnamese (bug #2).** The Arcade typing games showed
  ASCII-only prompts with no diacritics, and because they run inside KieeKey's own
  window — which the keyboard hook deliberately bypasses — the IME never composed
  there. Each game now owns a `VnComposer` (the same `TextEngine` the IME uses), so
  **Vietnamese is the default**: prompts bear full diacritics and the player types
  Telex/VNI exactly as configured while the game window composes and matches. An
  English mode keeps the legacy ASCII prompt. WasdRace moves steering to the arrow
  keys in VN mode (a/d/w/s are Telex letters). Settings → Arcade → *Ngôn ngữ đoạn
  văn* toggles VN↔EN. Proven by `tests/test_vn_composer.cpp` and
  `tests/test_arcade_vn.cpp` (real keystrokes → real composed Vietnamese).
* **Live external typing effects work again (bug #3).** The output path was unified
  into one allocation-free `planOutput()` contract (`LiveEffects.hpp`), proven by
  `tests/test_live_output_plan.cpp` (incl. a 40k-iteration fuzz).
* **Vietnamese can be typed in the in-app macro (*gõ tắt*) editor (bug #4).** The
  own-window hook bypass now exempts the macro edit control (published race-free as
  an atomic HWND from the UI thread), and live effects are force-disabled while it
  has focus so the stored expansion is clean text.
* **The "processed key events" counter no longer ticks on its own (bug #5).** A new
  `HookCounters` model counts keyboard events only (key/sys-key down/up); mouse,
  wheel and foreground changes are tracked separately (`tests/test_hook_counters.cpp`).
* **Chaos Lab is readable (bug #1, part 1).** The Lab created every control with
  **no font**, so its edit boxes fell back to the raster `SYSTEM_FIXED_FONT` and
  could not render Vietnamese diacritics. It now uses a DPI-scaled **Segoe UI** face
  applied to all children and rescaled on `WM_DPICHANGED`, and its clipped labels and
  truncated button were re-sized.

Also in this release: a portable dialog **layout solver** (`src/app/DialogLayout.hpp`)
that measures labels, grows the ones whose text does not fit, reflows the controls
below and grows the page — proven by `tests/test_dialog_layout.cpp` against the real
authored rectangles of the settings dialog (it reproduces the reported clipping, then
fixes it, idempotently). Wiring that solver into the settings dialog's `WM_CREATE`
(the remaining half of bug #1) is the tracked follow-up. An engine over-erase defect
(`visibleAccount_` not reset on a context change) was fixed, and the **Chẩn đoán**
(diagnostics) tab was expanded into a real debug surface with all-on / all-off /
basic modes and a rotating file log (`tests/test_diagnostics.cpp`).

A complete, runnable Windows x64 build is cross-compiled with Zig and committed at
`dist/KieeKeyApp.exe` for instant testing; the authoritative release binaries still
come from CI (MSVC, `/W4 /WX`, x64 + ARM64 + ARM64EC).

## What's new in v1.3.0-beta2 — Input isolation & reliability

Beta2 is a stabilization release. Arcade no longer captures keys from other
applications. The revised candidate (Windows file version **1.3.0.3**) adds
explicit, default-off **live external typing effects**: tray → **Hiệu ứng gõ
bên ngoài**, or Settings → **Phòng Chaos**. Random casing and Unicode flipped
glyphs apply to both literal characters and Vietnamese tone rewrites. Disable
instantly with **Ctrl+Alt+F12**; the IME must be ON, using Unicode, and app
exclusions still apply. This is separate from the Lab. Unsupported glyphs stay
unchanged; arbitrary 90°/270° rotations are not plain-text output.

Typing passages now use explicit clipped character cells, a scrolling caret
window and one viewport scale (no double-DPI font scaling). Typing Race has
correct Backspace/net-WPM and mistake feedback; Fishing has catch/escape/error
feedback and F2 restart; No-Mistake ignores nonprinting controls and shows an
accurate reserve meter. Native pause/result panels no longer overlap the Hub
title. Regression tests cover these paths, not a claim of all-editor support. See [CHANGELOG.md](CHANGELOG.md) and the
[Windows smoke-test checklist](docs/BETA2_TESTING.md).

The beta2 PR is held for manual Windows x64 testing before merge. Automated
checks do not establish compatibility with every editor or Windows version.

## What's new in v1.3.0-beta1 — Arcade Hub, Chaos Lab, AI Rival & Progression

**v1.3.0-beta1** expands KieeKey into a Vietnamese IME that is also fun to
type in: an isolated **Arcade Hub** of eight typing games, a **Chaos Lab** of
visual text transforms that can type into other applications, a **personal AI
typing rival** that learns your rhythm and then races you, **global
progression** (levels, XP, achievements) and a **real-time typing coach**.
Every optional module is off the hot path: when nothing is active the engine
performs the same as before (verified by the feature-isolation gate, see
[Verification](#verification-in-this-release)).

Everything below is one release; the full history of v1.2.x and v1.1.x lives in
[CHANGELOG.md](CHANGELOG.md).

### Graphical front-ends (the games are never text-mode)

| surface | what it is | how to open it |
|---|---|---|
| **Arcade Hub window** | Win32/GDI window (1180×760, double-buffered, 60 FPS) with a game-catalogue sidebar, click-to-play, live score/WPM footer, a level-up toast and — since beta5 — a 🧪 Chaos Lab shortcut under the eight games | `KieeKeyApp.exe --arcade[=slug]`, the tray menu, or the settings dialog buttons |
| **Web player** | HTML5 canvas client driving *the same C++ engine* over HTTP + SSE (`tools/arcade_serve`, `web/`) | `arcade_serve --port 8765 --host 0.0.0.0 --web web` then open `http://localhost:8765/` |
| **Chaos Lab window** | Dedicated test UI: type text, see the exact bytes KieeKey would emit, and (optionally) write them into the application you were working in | 🧪 **Chaos Lab row at the bottom of the Arcade Hub sidebar** (beta5), the tray menu → 🌀 Phòng Chaos Lab…, the Live-effects tab button, or `KieeKeyApp.exe --chaos-lab` — and if it ever fails to open, a message box now shows the Win32 error code instead of silently doing nothing |
| **Flexing page** | Its own surface inside the lab: prepared passage in, engine text out (WPM / efficiency / cursor), then really typed into the app you came from — one paste or chunk by chunk | Lab window → 🗿 Flexing Mode |
| **Web labs** | The same two surfaces in the browser: `POST /api/preload` + `flex.emitted` for the Flexing stage, `GET|POST /api/chaos` + `POST /api/chaos/preview` for the Chaos lab (the transformation runs in C++, never in JavaScript) | Buttons in the side panel of the web player |
| **Web progression & AI** | Level, XP bar, streak, achievements and minigame records from `ProgressionEngine`, plus the opt-in AI rival with the pace it learned and the finish time it would get on the passage you are racing — a level-up toasts in the page | Side panel of the web player |
| **Settings surfaces** | WinUI 3 pages (Arcade / Chaos / Progression & AI) and the Win32 settings dialog act as launcher + live telemetry, and carry the run configuration: Rhythm/No-Mistake **fail mode** (Hardcore default, or HP bar) and the **BPM** | `KieeKeyApp.exe --settings=N` |

### The eight games

| | game | what it tests | controls |
|---|---|---|---|
| 🐍 | **Snake** | reaction + control under acceleration | WASD / arrows, `P`/`F1` pause, `R`/`F2` restart, `Esc` quit |
| 🧱 | **Tetris** | 7-bag tetrominoes, rotation, line clears, levels | `A`/`D` move, `W` rotate, `S` soft drop, `Space` hard drop |
| 🎣 | **Fishing by typing** | type the shown bait; pull meter, escape timers, rarities from common to legendary | type the prompt |
| 🏎️ | **Typing-speed racing** | race a pacer on a WPM track | type the passage |
| 🛣️ | **Racing + WASD dodging** | multitasking: refuel by typing, dodge in three lanes | `A`/`D` lanes, `W`/`S` speed + typing to refuel |
| 🎵 | **Rhythm typing (FNF-style)** | beat-synchronised notes: too fast dies, too slow dies | `D`/`F`/`J`/`K` on the beat; **hardcore** (one miss = death, default) or **HP bar** mode, selectable in the UI |
| 🎯 | **No-Mistake Mode** | one wrong key is punished (configurable reserve/penalty) | type each character correctly |
| 🗿 | **Flexing Mode** | type anything and pre-prepared text appears at ludicrous speed — and can be injected into the focused app | any key |

### Chaos Lab — chaos you can actually test

* 🌀 **Random UPPER/lower case** — intensity and granularity (per character / per word),
  Vietnamese case tables (including `Ưư`), off by default.
* 🔄 **Rotated / flipped glyphs** — lookalike transforms (rotate, flip, random).
  90°/270° rotations are *render-only*: the document keeps pristine Unicode.
* Both can **type the result into the focused application** through the same
  `InlineEmitter` the IME uses, and the Chaos Lab window is the dedicated UI
  for testing exactly that (type, compare, inject).
* **Where to find it (beta5):** the 🧪 row under the eight games in the Arcade
  Hub sidebar, tray menu → *🌀 Phòng Chaos Lab…*, Live-effects tab →
  *Mở Chaos Lab*, or `KieeKeyApp.exe --chaos-lab`. Every entry point now
  reports a failure with its Win32 error code instead of silently doing
  nothing.

### AI rival, progression, analytics, ghost

* 🤖 **Personal AI typing rival** — learns inter-key intervals, pauses, bursts and
  tone-mark delay from your own keystrokes (explicit opt-in, local only,
  one-click purge), then races you with your own rhythm. Learning is
  incremental: each keystroke is fitted exactly once and the model is
  evidence-weighted, so it follows a fast typist and a slow one alike.
* 📈 **Global progression** — XP for characters, words and whole runs (snake,
  tetris, fishing, races, rhythm, no-mistake), a deterministic level curve,
  lifetime statistics, daily streaks, achievements and checksummed
  persistence with corrupt-file recovery.
* 📊 **Analytics & coach** — a fixed-size lock-free ring records your typing;
  the coach separates measured facts (“your mean IKI is 168 ms”) from heuristic
  advice, and only speaks once there is enough evidence.
* 🌐 **Solo-online / ghost** — a pluggable provider interface for replays,
  ghosts and leaderboards, with a fully local provider and zero network traffic.

### Verification in this release

Run everything with `tests/run_all_tests.sh` (native) or `ctest` on Windows:

| suite | covers | result |
|---|---|---|
| `ok_arcade_tests` | all 8 games, manager, live config, frame text ownership, determinism, no steady-state allocations | 3844 checks, 0 failures |
| `ok_arcade_render_tests` | display list, JSON wire format, injection safety, viewport letterboxing, payload budget | 6/6 |
| `ok_arcade_server_tests` | HTTP routing, session control, input forwarding, traversal guards, a full simulated race, the Flexing payload, the Chaos lab, progression/AI-rival routes, JSON string escapes, `POST /api/config` live/applyNow | 12/12 |
| `ok_arcade_window_tests` | **the real Win32 window procedures**: catalogue painting, hover, click-to-play, keyboard, timer, `Esc`/close, Chaos Lab preview + injection, and the Flexing page (prepared text in, engine text typed out) | 5/5 |
| `tests/web_labs_test.js` | the browser lab glue: engine text into the flexing control, chaos preview/replay, "keys stay in the text field" guard | 27 checks |
| `tests/web_progress_test.js` | the progression/AI panel: engine numbers, level-up toast, opt-in switch, learned pace, live race preview, visibility-aware polling | 23 checks |
| `tests/web_render_test.js` | the HTML5 renderer replayed over frames captured from the C++ engine (`tests/data/web_frames.json`): every game's commands, colours, gradients, HUD | 62 checks |
| `tests/render_web_frames.js` | the same fixtures rasterized with real Canvas2D into **pixel evidence** (`docs/bench/arcade-130/frames/*.png` + a contact sheet); needs the optional `@napi-rs/canvas`, otherwise it reports `skipped` | 8 games |
| `demo/arcade_cli --test` | the terminal front-end: all 8 games launch, answer input and produce a display list + wire JSON | 8/8 + chaos |
| `ok_chaos_tests` | case + glyph transforms, render-only rotations, thread safety | 4/4 |
| `ok_ai_tests` | opt-in/privacy, learning, racer determinism, ghost round-trip, hostile payloads, concurrency | 6/6 |
| `ok_progression_tests` | XP, levels, streaks, achievements, persistence/recovery, concurrency | 9/9 |
| `ok_analytics_tests` | rhythm metrics, ring rollover, session window, coach, concurrency | 6/6 |
| `ok_online_ghost_tests` | ghost/replay/leaderboard abstraction | ✓ |

The Windows-only UI code is exercised on any host through
`tests/win32_gdi_stub.cpp` — a recording USER32/GDI32 layer that delivers real
`WM_*` messages to the real window procedures and records every drawing call,
so “the sidebar lists all eight games”, “hovering highlights a row” and
“injection only happens when the checkbox is ticked” are assertions, not
claims. Standardised performance numbers for the arcade pipeline live in
[docs/bench/arcade-130/ARCADE_BENCH_REPORT.md](docs/bench/arcade-130/ARCADE_BENCH_REPORT.md).

**Core IME guarantees are unchanged**: 2,059,419-event gate correctness
against the clean-room oracle (0 mismatches), 0 ns added latency when the
optional modules are idle, and unchanged peak RSS. The v1.3.0 line was also
A/B-measured against the **v1.2.2 stable tag** with byte-identical harnesses —
see [docs/bench/v122_vs_130/](docs/bench/v122_vs_130/README.md): equal or faster
on every end-to-end percentile (p50 −5.2 %, p99 −1.5 %), identical peak RSS
(8.551 MB), the same SendInput batching invariant (108 batched edits per
100 000 keys), and a byte-identical three-engine differential.

## Highlights

* **Correct Vietnamese output** — Telex, VNI and Simple-Telex, with the
  upstream tone-mark fix carried in (golden test: `"as"` → `"á"`).
* **Low latency by design** — asynchronous low-level keyboard hook feeding a
  lock-free Vyukov SPSC ring; the hook callback never blocks and returns in
  O(1). Deep E2E latency audit included ([docs/reports/LATENCY_AUDIT_REPORT.md](docs/reports/LATENCY_AUDIT_REPORT.md)).
* **Modern TSF composer** — commits text through the Text Services Framework
  text store, no synthetic backspaces.
* **Event-driven app awareness** — foreground-process monitor with
  auto-exclusion of games/apps that dislike IMEs.
* **Familiar tray UX** — green/gray tray icon, right-click menu, Vietnamese
  settings dialog, explicit in-app on/off toggle (tray menu + settings
  button; the old Ctrl+Shift global hotkey was removed in v1.1.1).

## What KieeKey changes compared to OpenKey

| Area | OpenKey 2.0.5 (upstream) | KieeKey v1.3.0 |
|---|---|---|
| Language level | C++11 / Win32 | C++20/23 (per-instance state machine, constexpr, RAII) |
| Input pipeline | Synchronous processing in hook callbacks | Async hook thread → lock-free ring → consumer thread |
| Text insertion | Backspace-driven editing | TSF text-store composer (single-edit fast path) |
| Phonetics data | `std::map`/`std::vector` built at runtime | Generated flat tables (`tools/gen_flat_tables.py`) for cache-friendly lookups |
| Resource handling | Manual HANDLE/registry lifecycle | RAII wrappers (`Win32RAII.hpp`) |
| Testing | Manual QA | Golden vectors + stress/soak/fuzz suites + 3-way differential vs a clean-room oracle and vendored engines (~5.97M cases, [docs/reports/MEGA_BENCH_REPORT.md](docs/reports/MEGA_BENCH_REPORT.md)) |
| Latency engineering | — | Instrumented tone-mark path, event-driven ordering barrier, p50–p99.9 percentile benches ([docs/reports/LATENCY_AUDIT_REPORT.md](docs/reports/LATENCY_AUDIT_REPORT.md)) |
| CI/CD | — | GitHub Actions matrix: x64 + ARM64 + ARM64EC, unit tests (ctest) on the native-arch x64 job, on every push |

> KieeKey keeps the engine algorithm faithful to upstream (1:1 phonetics
> tables) — the refactor targets architecture, latency and testability, not
> behavior changes. The full engineering history (15 verbatim lineage
> reports, written under the pre-release working name "OpenKey NextGen")
> is preserved in [docs/reports/](docs/reports/README.md).

## Architecture

```
WH_KEYBOARD_LL / WH_MOUSE_LL          (hook thread, serialized)
        │  try_push — O(1), allocation-free
        ▼
LockFreeQueue (Vyukov SPSC ring)
        │  drain
        ▼
TextEngine  ── Telex / VNI / Simple-Telex state machine (flat tables)
        │  OutputItem (backspace count + UTF-16 payload)
        ▼
TsfComposer ── TSF text-store commits (zero-alloc single-edit fast path)
        │
        ├── ProcessMonitor ── foreground app detection + auto-exclusion
        ├── Win32 tray app (KieeKeyApp.exe) + optional WinUI 3 settings UI
        │
        └── optional side features (off the hot path, same core)
              ArcadeManager ── Frame ── buildRenderList ──┬── Win32 GDI hub window
                                                          └── JSON/SSE ── HTML5 canvas
              ChaosEngine · AiRivalEngine · ProgressionEngine · TypingAnalytics
```

Source layout: `src/core` (engine, queue, RAII, tables **and** the arcade,
chaos, AI, progression, analytics and ghost modules), `src/tsf` (composer),
`src/app` (Win32 tray app, resources, the GDI Arcade Hub window and the Chaos
Lab window), `src/ui` (WinUI 3 Fluent settings), `web` (HTML5 Arcade client),
`tests` (unit/stress/bench harnesses + vendored reference engines),
`tools` (table generators + `arcade_serve`, the web bridge), `demo`
(interactive console demos).

## Building

Requirements: **Windows 10/11**, **Visual Studio 2022** (Desktop C++),
**CMake ≥ 3.28**, Windows App SDK (only for the optional WinUI 3 UI).

```bat
:: configure (x64 Release, WinUI 3 UI + tests)
cmake --preset x64-release

:: build
cmake --build --preset x64-release

:: run the unit tests via CTest
ctest --preset x64-release
```

The native Arcade Hub needs no extra dependency (GDI only). The optional web
player is a second target that builds on Windows **and** on Linux:

```bat
cmake --build --preset x64-release --target arcade_serve
arcade_serve --port 8765 --host 0.0.0.0 --web web
```

#### Running the player continuously (your own machine)

A hosted preview is convenient but **ephemeral** — it dies with the machine that
serves it. To keep the Arcade Hub player running for real, run the bridge on a
box you control (PC, laptop, VPS, NAS); the process keeps the C++ engine in
memory and serves `web/` over the LAN:

```bat
:: Windows — builds arcade_serve if needed, restarts on crash, prints the LAN URL
powershell -ExecutionPolicy Bypass -File scripts\run_web_bridge.ps1 -Port 8765 -RestartAlways
```

```bash
# Linux/macOS
PORT=8765 bash scripts/run_web_bridge.sh
```

Both launchers accept the bind address and tick rate (`HOST`, `FPS`; `-Bind`,
`-Fps`), and the Windows one can be registered once as an at-logon task so the
player survives reboots:

```bat
schtasks /create /tn "KieeKey Arcade Bridge" /sc onlogon /rl highest ^
  /tr "powershell -WindowStyle Hidden -File \"%CD%\scripts\run_web_bridge.ps1\""
```

The release artifact for each architecture now ships everything that is needed
(`arcade_serve.exe`, `arcade_bench.exe`, `web/`, both launcher scripts), so a
download-and-run needs no toolchain. On Windows prefer the native hub
(`KieeKeyApp.exe --arcade`) when you only need it locally — the web bridge is
the option that also works head-less and remotely.

Presets available: `x64-debug`, `x64-release`, `arm64-release` (see
`CMakePresets.json`). A MinGW-w64 toolchain file is provided at
`cmake/mingw-w64-x86_64.cmake` for console/engine-only builds. CI builds
both targets on every push via `.github/workflows/build.yml`; tagged commits
additionally produce downloadable release artifacts — prebuilt binaries are
**not** committed to this repository (see `bin/README.txt`). The optional
WinUI 3 front-end is not built in CI (it needs the Windows App SDK NuGet
package restored locally).

### Keeping CI green: the `SHA256SUMS.txt` manifest

CI verifies `SHA256SUMS.txt` against the tree on every push — a commit that
changes any tracked file without regenerating the manifest fails the
`Verify SHA256SUMS manifest` step (this is exactly how the two CI breakages
after README-only edits happened). The manifest hashes the **staged** (index)
content, so regenerating is a single pre-commit step, not a second commit:

```bash
git add -A                        # stage everything you changed
scripts/gen_sha256sums.sh         # regenerate against the staged tree
git add SHA256SUMS.txt
git commit                        # manifest + tree are consistent
```

To make it impossible to forget, enable the bundled pre-commit hook once
per clone — it regenerates and re-stages the manifest automatically
whenever a commit touches tracked files:

```bash
git config core.hooksPath scripts/hooks
```

## Repository layout

```
KieeKey/
├── LICENSE                     GNU GPLv3 (verbatim)
├── README.md                   this file
├── THIRD-PARTY-NOTICES.md      upstream licenses & compliance notes
├── CHANGELOG.md                release history
├── CMakeLists.txt / CMakePresets.json / cmake/
├── .github/workflows/build.yml CI matrix (x64/ARM64)
├── docs/reports/               15 verbatim lineage engineering reports (+ index)
├── docs/bench/                 raw benchmark artifacts per campaign
├── src/                        core engine, arcade/chaos/AI modules, TSF, tray app, WinUI 3
├── web/                        HTML5 Arcade Hub client (canvas + SSE)
├── tests/                      unit / stress / bench + vendored references
├── tools/                      flat-table generators + arcade_serve web bridge
├── imebench_kit/               3-way benchmark harness (results regenerated locally)
├── demo/                       interactive console demos (engine + arcade)
├── scripts/                    engine probes + SHA256SUMS tooling & hooks
└── bin/                        build-output placeholder (see bin/README.txt)
```

## License

KieeKey — Copyright (C) 2026 coderunknow - https://github.com/coderunknow.

This program is **free software**: you can redistribute it and/or modify it
under the terms of the **GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version** — see the [LICENSE](LICENSE) file.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.

KieeKey is a modified version based on OpenKey — Copyright (C) 2019
Tuyen Mai — which is licensed under GPL-3.0; KieeKey inherits that license
in full. Third-party reference sources are vendored verbatim under their
original licenses — see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Acknowledgements

* **[OpenKey](https://github.com/tuyenvm/OpenKey)** by **Tuyen Mai** — the
  upstream project KieeKey is based on. KieeKey would not exist without the
  original engine, its phonetics tables and its years of real-world
  refinement. Xin cảm ơn!
* **[UniKey](https://www.unikey.org)** by **Pham Kim Long** — vendored as a
  reference engine used by the differential-test harness to cross-validate
  correctness (LGPL; original headers preserved).
* The Vietnamese free-software community, whose feedback shaped both
  upstream projects.

---

## Tóm tắt (Tiếng Việt)

**KieeKey v1.3.0-beta7** là bộ gõ Tiếng Việt cho Windows, xây dựng dựa trên
**[OpenKey](https://github.com/tuyenvm/OpenKey)** (GPL-3.0) của tác giả Tuyen
Mai. Engine gốc đã được port sang C++ hiện đại: hook bất đồng bộ với hàng đợi
lock-free, composer TSF (không backspace ảo), bảng âm tiết flat tối ưu cache,
kèm bộ test vi mô, đo hiệu năng và đối chiếu sai khác hàng triệu trường hợp.

Điểm mới của v1.3.0: **Arcade Hub** với 8 mini-game gõ phím (Rắn, Xếp gạch,
Câu cá, Đua tốc độ, Đua + né WASD, Nhịp điệu FNF, Không-lỗi, Flexing) chạy
trong **cửa sổ đồ hoạ thật** (Win32 GDI trên desktop, HTML5 canvas trên web —
cùng một engine C++); **Phòng Chaos** đổi hoa/thường và xoay/lật glyph (có
thể gõ thật ra ứng dụng đang mở, kèm giao diện test riêng); **AI đối thủ** học
nhịp gõ của bạn rồi đua lại; **tiến trình** cấp độ/XP/thành tựu; **thống kê &
huấn luyện viên** theo thời gian thực; và **ghost/online** dạng pluggable chạy
hoàn toàn cục bộ. Mọi tính năng mới đều ở ngoài đường găng: khi không dùng,
độ trễ gõ không đổi.

Điểm mới của v1.3.0-beta5: sửa tận gốc **cả 9 nhóm lỗi** trong báo cáo thử
nghiệm beta4 — hộp thoại cài đặt chồng lấn (tự dàn trang + cuộn), hiệu ứng gõ
trực tiếp kèm chỉ báo cổng chặn, bộ đếm chẩn đoán đúng từng nguồn, tên ứng
dụng hết "unknown", game hiện đuôi gõ sai màu đỏ + gợi ý Backspace,
No-Mistake kết thúc đúng khi sai từ cuối + WPM trực tiếp, chọn **phím lái
WASD khi gõ tiếng Việt**, Chaos Lab có lối vào ngay trong Arcade Hub, và mọi
tùy chọn cài đặt **áp dụng ngay khi bấm + được lưu**. Thêm tài liệu hiệu năng
`docs/PERFORMANCE.md` (chi phí ns/phím của từng tính năng opt-in). Chi tiết:
`docs/release-notes-v1.3.0-beta5.md`.

Điểm mới của v1.3.0-beta7 — bản sửa tính đúng đắn của chẩn đoán (Windows **1.3.0.8**): báo cáo beta6 tự mâu thuẫn — `dpi: 96` vs `display-metrics 144`, `SendInputCalls: 0` vs 13 dòng emit-chain, mọi trường runtime kẹt ở `0`/trống — vì `SystemSnapshot` chưa từng được làm tươi, nhánh inline chưa đếm `SendInputCalls`, và các đếm vòng/đánh thức chỉ nằm trong wrapper. Bản này làm tươi toàn bộ snapshot, đồng bộ đếm sống vào báo cáo trước mọi xuất/sao chép/kiểm tra nhanh, và ghi chú nguồn gốc cho `96`/`0`. Chi tiết: `docs/release-notes-v1.3.0-beta7.md`.

Điểm mới của v1.3.0-beta6 — bản tự kiểm tra, chuẩn bị cho **v1.3.0-rc1**
(không có báo cáo thử nghiệm mới — KieeKey tự tìm lỗi của chính mình, mọi kết
luận đều gắn nhãn `[VERIFIED]`/`[HYPOTHESIS]` trong
`docs/release-notes-v1.3.0-beta6.md`):

* **Đóng cổng CI**: audit dàn trang hộp thoại (`--strict`) và audit điều
  khiển nay chạy trong bộ test (trước đây chỉ pass vì có người chạy tay);
  cả hai cổng được chứng minh bắt được lỗi gieo trước khi được tin cậy.
* **Cân bằng trình phát web**: chọn **phím lái** (B7) ngay trong bản web;
  đuôi gõ sai màu đỏ + gợi ý "Backspace để sửa" (B5) nay THẬT SỰ hiển thị
  trên web (trước đó bị dòng trạng thái đè); chỉ báo cổng chặn hiệu ứng
  được ghi rõ là chỉ có ở bản Win32.
* **Phòng Chaos được gia cố**: toàn bộ 17 điều khiển được kiểm tra tận gốc
  (tạo → xử lý → engine → lưu); sửa lỗi làm tròn % của thanh cường độ;
  cường độ / chế độ glyph / độ hạt nay sống sót qua khởi động lại; audit
  hợp đồng nguồn mới bảo vệ cửa sổ này trong bộ test.
* **Chẩn đoán có bằng chứng cho người thử máy**: báo cáo chẩn đoán (xuất
  file hoặc **nút "Sao chép báo cáo"** vào clipboard) nay kèm các dòng đọc
  được bằng máy — 32 lần xuất chữ gần nhất kèm class cửa sổ/tiến trình đích
  và phán quyết cổng hiệu ứng tại thời điểm xuất, Win32 API nào đã giải tên
  tiến trình nền trước, và chỉ số DPI/font/DWM thật.
* **Benchmark chạy trên CI**: chiến dịch A/B chống thụt lùi đầy đủ (trung vị
  cô lập tính năng + đường ống e2e shim) nay chạy bằng GitHub Actions trên
  máy yên tĩnh hơn, trả nốt món kiểm tra e2e của beta5.

Chạy toàn bộ kiểm thử: `tests/run_all_tests.sh` (Linux/mac) hoặc `ctest`
(Windows). Bật/tắt bộ gõ nằm hoàn toàn trong ứng dụng (trình đơn khay + Cài
đặt); mở nhanh game bằng `KieeKeyApp.exe --arcade`, mở phòng Chaos bằng
`KieeKeyApp.exe --chaos-lab`.

KieeKey là phần mềm tự do theo **GNU GPLv3** (kế thừa từ OpenKey). Bản quyền
tác giả gốc được giữ trong đầu mỗi file nguồn; mã tham chiếu OpenKey 2.0.5 và
UniKey giữ nguyên trong `tests/reference/` — xem
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

<!-- CI benchmark trigger -->
