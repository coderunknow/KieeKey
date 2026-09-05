# KieeKey

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Language](https://img.shields.io/badge/language-C%2B%2B20%2FC%2B%2B23-00599C.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20ARM64-0078D6.svg)
![Build](https://img.shields.io/badge/build-CMake%20%3E%3D%203.28-064FAD.svg)

**KieeKey v1.2.1 RC3** is a modern, low-latency Vietnamese input method
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

## What's new in v1.2.1 RC3 — stability, correctness & a leaner engine

* **Pipeline bug fixed: pending-count underflow after a wedged lifecycle
  drain.** `PendingEditCounter::consume()/rollback()` used a bare
  `fetch_sub`; when a lifecycle `forceQuiesce()` (engine off, auto-excluded
  app, power resume) raced a consumer batch that had already popped its
  edits, the counter wrapped to ~4.29 billion, read as "hugely pending", and
  every following pass-through keystroke burned the full ordering-barrier
  budget — the post-wedge "typing lags, then it goes away" symptom.
  The release is now a saturating CAS-clamped release (a concurrent publish
  can never be lost); reproduced 5/5 by the pipeline soak on a 2-CPU host,
  fixed + pinned by a deterministic regression test.
* **Engine decision latency −23 % … −46 % p50** across all four
  real-world workloads of the micro benchmark (vn-compose 65 → 50 ns,
  mixed 77 → 51 ns, passthrough 82 → 44 ns, delete 65 → 46 ns), and
  **−29.5 %** on the 20 M-key throughput driver — from ONE optimization:
  the two consonant table walks in `checkSpelling` (still 40 % of engine
  time after RC2's own pass) now use constexpr first-letter row buckets,
  visiting at most 4 of 33 leading rows and 3 of 11 end rows. Proven
  **decision-identical to RC1/RC2** on 4.8 M lockstep differential events
  (4 seeds × 1.2 M) with the byte-identical output sink.
* **Version-consistency gate extended**: the public
  `OPENKEY_KIEEKEY_VERSION_*` macros in `kieekey_core.hpp` had drifted to
  `1.2.0` through RC1 and RC2 — every carrier now agrees on
  `1.2.1 RC3` and the check script covers the macros.
* 21 native suites PASS (RC2 baseline on the same host: 20/21 — the
  pipeline soak failed 5/5); ASan/UBSan clean on engine, pipeline and
  differential harnesses.

Full evidence, RC2-vs-RC3 tables and the honest no-regression analysis:
[docs/reports/V1.2.1_RC3_RELEASE_REPORT.md](docs/reports/V1.2.1_RC3_RELEASE_REPORT.md)

## What's new in v1.2.1 RC2 — performance, optimization & real-world hardening

* **Engine decision latency −28 % … −61 %** on every real-world typing
  workload and percentile (200 WPM burst p50 161 → 75 ns, p99.9 456 → 191 ns),
  E2E pipeline burst p50 −9.9 % / p99 −6.9 %, memory flat — with the engine
  proven **decision-identical to RC1** on 1.2 M lockstep events against the
  frozen RC1 engine (`tests/diff_engine_ab.cpp`).
* **Performance Preference Profiles** (settings → *Chế độ xuất & hiệu năng*):
  Cân bằng (default = RC1 behaviour), Nhanh nhất, Ít nháy chữ nhất, Chính xác
  tối đa, Tự động thích ứng, plus Tiết kiệm CPU / Từ điển hybrids — one
  centralized strategy that really changes output path, spin, barrier, batch
  and correctness policy. Each profile is benchmarked.
* **Smart notifications** with cooldown, dedup, hourly caps and a persisted
  *Don't show again* — first use: *"Telex nhanh đang sửa nhầm từ của bạn?"*
  when the general quick-Telex detector sees you repeatedly undoing
  `cc/gg/kk/nn/qq/pp/tt` expansions (**Turn off / Keep / Don't show again**).
* **3 bugs fixed** with permanent regression tests, incl. Ctrl/Alt shortcuts
  (Ctrl+`,` Ctrl+Enter…) that were swallowed after a non-Vietnamese word.
* New 7-scenario stress battery (`tests/stress_rc2.cpp`); 21 native suites.

Full report with RC1-vs-RC2 tables (faster / unchanged / slower on every
row), profiling, rejected optimizations and methodology:
[docs/reports/V1.2.1_RC2_PERFORMANCE_REPORT.md](docs/reports/V1.2.1_RC2_PERFORMANCE_REPORT.md)
— raw artifacts in [`docs/bench/`](docs/bench/).

## What's new in v1.2.0 Stable

**Two releases in one version:** the v1.2.0 feature + performance release,
followed by a **stability / correctness / real-world-UX hardening pass**
(the "Stable" suffix — this is what v1.2.0 Stable ships).

### The stability pass (recommended reading first)

An IME is a system service the user reaches through their fingers, so the
release goal was **real-world reliability, not benchmark numbers**: two
changes were accepted that cost a sub-nanosecond amount of throughput because
they remove a user-visible failure mode.

1. **The IME can no longer die mid-keystroke.** Any exception escaping the
   consumer handler — a plain `std::bad_alloc` under memory pressure is
   enough — used to call `std::terminate()` inside a `noexcept` frame. The
   process vanished while the tray icon stayed behind, and every window then
   produced raw, uncomposed Vietnamese with no visible signal. The handler is
   now fault-isolated: one bad event is dropped and counted, the pipeline
   keeps running.
1. **"Typing `p` twice turns it into `ph`" is fixed.** With "gõ tắt"
   (quickTelex) enabled, the cluster shortcut fired at *any* position in a
   word instead of only at the start of a syllable, so ordinary English words
   were silently mangled: `happy→haphy`, `apple→aphle`, `letter→lether`,
   `account→achount`, `running→runging`, `success→suches`, `cc→ch`, and 31
   more. Vietnamese clusters (ch, gi, kh, ng, ph, qu, th) are always
   syllable-initial, so the rule now requires the doubled pair to be the
   word's first two letters — which keeps 100% of the intended feature
   (`ppongf→phòng`, `ccaof→chào`) and makes every word-medial false positive
   impossible.
2. **Silent data loss on every caret move is fixed.** Every caret-moving
   keystroke (arrows, Home/End, PgUp/PgDn, Ins, Ctrl+Backspace) and every
   mouse click queues a resync, and each one used to inflate the counter that
   is the **only** clamp on how many characters a correction may delete. A
   later edit could therefore delete text to the LEFT of the word that the
   engine never committed — silently, with no error anywhere. Repro: click
   into a partially typed word, press Right a few times, finish the word.
3. **No more 1 ms stalls after a lifecycle transition.** Switching the engine
   off, landing in an auto-excluded app, or resuming from sleep could leave
   the ordering barrier's count armed with nothing left to wake the consumer,
   so every following keystroke paid the full barrier budget and arrived out
   of order. Lifecycle drains now poke the consumer, wait with a bound, and
   force quiescence only when it is genuinely wedged.
4. **Sleep/resume, lock/unlock and display changes are handled.** The tracked
   Shift/Ctrl/CapsLock bits, the cached keyboard layout and the per-app
   exclusion policy are all re-established — a modifier released while the
   secure desktop owned the keyboard is never seen as a key-up.
5. **DPI: the settings dialog only used to resize its frame**, leaving every
   child control and font at the old DPI. It now re-scales position, size and
   font, backed by a font cache that never evicts an in-use font.
6. **Testing: CTest went from 3 targets to 15**, with hard timeouts. Twelve
   harnesses existed in `tests/` and were never executed by any automated
   gate, so "full suite green" previously meant only "everything that ran,
   passed". Four new suites add ~18 500 assertions, including
   `tests/test_key_correctness.cpp` — which answers the question no other
   harness asked: *"I pressed these keys, is the text on screen what it should
   be?"* It models the shipped hook's consumer contract line-for-line and
   exhaustively checks all 18 278 one-to-three-letter sequences for silent
   character loss.
7. **The test and benchmark suites are much faster to run.**
   `tests/run_all_tests.sh` went **84 s → 35 s**: `--jobs` was parsed but
   never used (everything was sequential) and `TextEngine.cpp` was recompiled
   for all 13 engine-linked targets instead of once.
   `tests/bench_tone_latency.cpp` went **866 s → 134 s** by default, with
   `--fast` (~50 s) and `--full` (the exhaustive matrix) available.

**Measured against a frozen pre-work baseline:** mean Δp50 **+0.28 %**,
mean Δp99 **+0.19 %** (same-tree A/B — well under a nanosecond per key),
tone-population latency unchanged where it matters (`mixed`/TSF 101.94 →
101.98 µs), throughput 13 502 → 13 531 keys/s, memory flat. No known P0 or P1
remains at release.

Full engineering detail:
[docs/reports/V1.2.0_STABLE_RELEASE_REPORT.md](docs/reports/V1.2.0_STABLE_RELEASE_REPORT.md)
(baseline:
[docs/reports/V1.2.0_STABLE_BASELINE_REPORT.md](docs/reports/V1.2.0_STABLE_BASELINE_REPORT.md)).

### The feature + performance release

Three headlines:

1. **Macro expansion at printable punctuation (opt-in).** A new
   `EngineOptions::macroExpandsOnPunctuation` switch (default OFF — legacy
   byte-parity with OpenKey 2.0.5 is preserved) makes a macro break at
   `, . ; / ' \\ - =` expand the abbreviation AND keep the punctuation
   visible: typing `xl,` now yields `xin lỗi,` instead of swallowing the
   comma, and the macro-key accumulator is reset so consecutive `xl, xl,`
   both expand. (Legacy default keeps the 2.0.5 behavior byte-for-byte.)
2. **VIQR macro parity.** `EngineResult::macroExpansionUtf16` now renders
   macro expansions through the VIQR pipeline when
   `OutputEncoding::Viqr` is selected — precomposed Vietnamese no longer
   leaks as raw Unicode into pure-ASCII VIQR output.
3. **Grammar-gate speedup.** A `wordHasTransform_` eligibility flag skips
   the per-key `checkGrammar()` re-scan whenever the current word has never
   received a transform — provably no-op work removed from the hot path
   (monotone within a word, conservative on restore).

Measured evidence (this release's benchmark report,
[docs/reports/V1.2.0_BENCHMARK_REPORT.md](docs/reports/V1.2.0_BENCHMARK_REPORT.md)):
engine decision p50 ≈ **118–137 ns/key** (down ~5–18 % vs v1.1.3 on the
same host, 2M keys × 3 interleaved runs), shim-pipeline burst p50 ≈
**25.8 µs** (down ~16 %), tone-population parity, and a new deterministic
correctness gate (2.06M events, engine vs clean-room oracle) passing on
both versions before any latency number is trusted.

Benchmark tooling (kept in-repo for future releases): the hardened micro
harness `imebench_kit/harness/bench_perf.cpp` (v2 — warm-up, session reset,
sorted percentiles, T0-control overhead floor, full-distribution JSON), the
multi-run `--runs/--json` integration harness `tests/e2e_bench.cpp`, the
differential correctness gate `tests/gate_correctness.cpp`, and the
single-command suite runner **`tests/run_bench_suite.sh`** which builds all
targets, writes the machine-readable environment record, runs gate +
micro + integration + tone layers, and aggregates everything into
`summary.json`.

### What was in v1.1.3

**A quality/performance hardening pass over the whole input pipeline —
accuracy, latency, stability, robustness — with no behavior changes beyond
the fixes.** Headlines: the CapsLock toggle tracker no longer desyncs under
auto-repeat (the real root cause of the "tone marks make letters uppercase"
family), a phantom pending-edit race that could put a 1 ms stall on every
keystroke is gone, Vietnamese keystrokes are passed through untouched in
ELEVATED applications instead of silently vanishing, the self-heal watchdog
can now detect a dead keyboard hook even while the mouse is in use, and the
mega differential suite closed at **0 divergences across ~268M events**
(the independent oracle was re-aligned to the documented v1.1.0 contracts).
Full engineering detail in
[docs/reports/V1.1.3_HARDENING_REPORT.md](docs/reports/V1.1.3_HARDENING_REPORT.md)
and the [CHANGELOG](CHANGELOG.md).

### What was in v1.1.2

**Numbers are numbers — the digit bug is fixed.** The fatal report: typing
a number mid-word applied a Vietnamese tone mark to the word before it
("nhan5" → "nhạn") or turned it into another word ("d9" → "đ"). In VNI
mode every digit 1–0 is a composition key (tones, vowel marks, đ, tone
removal) — correct for classic VNI typists, but a trap for everyone else.
v1.1.2 ships the **"Số 0–9 luôn là chữ số"** option, ON by default: digits
ALWAYS type the literal digit in every input method, and the old behavior
is one checkbox away for VNI purists. Telex and Simple Telex are
byte-identical to before (digits already passed through there).

**v1.1.2-r3 root-cause closure (re-verified build, same version):** the
report still came back after r2, so every remaining hiding place was
closed. The **WinUI 3 front-end** (`src/ui`) turned out to have none of
the fix — it built its engine with legacy options (digits compose in VNI)
and never even applied the saved input method; it now constructs the
engine from the persisted options (digits policy + the same
`SettingsMigration` self-heal — both front-ends share one registry key)
and gained the digits checkbox. The **library default flipped**: 
`EngineOptions::digitsAreLiteral` is now TRUE so every `TextEngine{}` 
consumer gets digits-are-numbers by construction (legacy-parity harnesses
pin `false` explicitly; new vectors fail if the default ever flips back).
The Win32 hook gained a **NUMBER-SAFETY GUARD** — with the policy ON, no
engine path can ever edit text on a bare digit (discarded + counted,
expected 0). And because the symptom also survives a perfect patch when
**another IME (EVKey/UniKey/…) or the Windows built-in Vietnamese
Telex/VNI keyboard layout** converts digits instead, KieeKey now detects
both and names the culprit in the welcome balloon and in a new live
diagnostics block on the Information tab (running version + engine state +
digits policy + conflict verdict — instant proof of which build you run).

**v1.1.2-r2 hardening (re-verified build, same version):** the safe
default now lives in the app layer itself (a denied/corrupted registry can
no longer ship digits-as-composition), a one-time `SettingsMigration`
self-heal re-asserts the digits policy on upgrade, every settings-dialog
read is fail-safe (`dlgChecked` — a half-built control can never silently
flip a persisted option), and `scripts/audit_controls.py` guards the
control-id set. The exhaustive digit battery in `tests/test_textengine.cpp`
and the digit-path benchmark (`tests/bench_digits.cpp`, 36–40 ns/key)
close the loop; full-budget verification reports are in `docs/reports/`.

Also in v1.1.2:

* **Information tab** — the settings dialog gained a fifth tab,
  "Thông tin", introducing the app: what it is, its feature list, a
  quick-start guide, origin & GPLv3 licensing, and a clickable link to the
  repository. The tray menu gained a matching "Thông tin & giới thiệu"
  item that opens the dialog straight on that tab.
* **Modernized settings dialog** — an always-visible header (icon, app
  name, version, live status line: engine on/off + method + digits policy),
  grouped "Phương thức gõ / Tùy chọn gõ / Chế độ xuất" boxes, a wider
  layout for every tab, and the in-app ON/OFF button promoted to a bold,
  easier-to-hit control.
* **Consistency & test hygiene** — the test CHECK macros no longer use
  non-conforming `if constexpr` with runtime conditions (they hard-error
  on GCC/clang; plain `if` + the project-wide `/wd4127` is correct on every
  compiler), the mega differential vs the 2.0.5 engine re-verified at
  ~4.7M events with **0 divergences**, the engine still allocates **zero
  heap memory per keystroke** in steady state, and the hot path stays at
  ~100 ns/key.

### What was new in v1.1.1

**The Ctrl+Shift global hotkey was removed — on purpose.** It was the root
cause of the recurring "the IME suddenly turns off for no reason / I don't
know why, nothing shows up" report: a bare Ctrl+Shift press+release is also
the Windows language-switch chord and the prefix of dozens of application
shortcuts, third-party tools inject it, and a missed key-up made unrelated
chords fire — silently switching the input method OFF mid-work with no
visible feedback. In v1.1.1 the keyboard hook can no longer toggle the IME
at all. Instead, on/off is an explicit, always-visible in-app control:

* **Tray menu** — the first item states the action it performs:
  "Tắt gõ tiếng Việt" while running, "Bật gõ tiếng Việt" while stopped.
* **Settings dialog** — an always-visible toggle button on the button row,
  reachable from every tab and refreshed live.
* **Feedback** — every on/off change shows a tray balloon confirming the new
  state; the tray icon (green/gray) and tooltip mirror it.
* **Persistence** — the state is written to the registry at every change
  point AND on clean exit/logoff, so relaunching can never resurrect a stale
  on/off value (the "exit and open again, it shows the old state" report).
* The `CtrlShiftChord` state machine, its extreme-toggle test harness and
  every hotkey-facing string were removed; all version strings are now
  consistently **v1.1.1** (app header, VERSIONINFO, manifest, CMake,
  public `OPENKEY_KIEEKEY_VERSION_STRING`).

### What was new in v1.1.0

Fixes the reported "IME suddenly turns off for no reason / I don't know how
to open it again" family, on top of the "tone marks make my letters
uppercase" consumer-layer fixes. Three phantom-toggle paths in the
Ctrl+Shift chord state machine were eliminated (a cumulative-contamination
model replaces the per-event state reset; injected modifier events are
ignored; a foreground-change resync can no longer arm an unobserved chord),
and every toggle now shows a tray balloon confirming the new state and how
to undo it — including a startup balloon when the app comes up in the
persisted OFF state. The hook's tracked Shift/CapsLock state is re-seeded
from the OS on every foreground change and injected toggle keys are
tracked. Full list in [CHANGELOG.md](CHANGELOG.md).

### v1.1.0 feature highlights

A broad reliability, correctness and UX release. Highlights:

* **Correctness** — fixed the `size_t` OOB read at word start, the dead
  (impossible) modern-orthography mark rules carried verbatim from 2.0.5,
  the click/caret **context-resync desync** (raw words like `as`/`dd`/`aw`
  used to leave phantom transform state and duplicate letters), stale VNI
  vowel indices, and the macro/`backspace` bookkeeping desync.
* **Real macros ("Gõ tắt")** — the v1.0.x checkbox was a silent no-op; v1.1.0
  ships a macro table loaded from `%APPDATA%\KieeKey\macros.txt`, editable in
  the new settings tab (`abbr=expansion` per line).
* **Hook** — CapsLock/NumLock/ScrollLock are now tracked live (toggling after
  launch no longer composes the wrong case), the Ctrl+Shift chord re-syncs on
  app switch (no more self-toggling after a missed key-up), mouse-wheel breaks
  the word, AltGr (Ctrl+Alt) composes normally, dead-key layouts flush
  correctly, numpad digits are mapped.
* **TSF** — edit-session objects are released (no more per-keystroke COM
  leak in browsers/Office), the caret read-back session is synchronous and
  owns its buffer (no dangling stack write), and a **slow-commit watchdog**
  downgrades a starving foreground to inline SendInput before it can wedge
  the pipeline.
* **Stability** — stuck-worker shutdown now publishes a "detached" state so
  the process exits via `ExitProcess` (no static-destruction race), and the
  ordering-barrier deadline moved to QPC (the 1 ms budget is real even
  without `timeBeginPeriod`).
* **UI/UX** — DPI-aware settings dialog (per-monitor scaling), the clipped
  "Luôn SendInput" radio is fixed, diagnostics gained barrier-timeout /
  hook-self-heal / TSF-slow-commit / current-app rows, the latency peak
  resets on open, the WPM gauge decays when idle, Vietnamese on/off state is
  persisted across restarts, and the tray tooltip explains auto-excluded
  apps.

---

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

| Area | OpenKey 2.0.5 (upstream) | KieeKey v1.1.2 |
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
        └── Win32 tray app (KieeKeyApp.exe) + optional WinUI 3 settings UI
```

Source layout: `src/core` (engine + queue + RAII + tables), `src/tsf`
(composer), `src/app` (Win32 tray app + resources), `src/ui` (WinUI 3
Fluent settings), `tests` (unit/stress/bench harnesses + vendored
reference engines), `tools` (table generators), `demo` (interactive
console demo).

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

Presets available: `x64-debug`, `x64-release`, `arm64-release` (see
`CMakePresets.json`). A MinGW-w64 toolchain file is provided at
`cmake/mingw-w64-x86_64.cmake` for console/engine-only builds. CI builds
both targets on every push via `.github/workflows/build.yml`; tagged commits
additionally produce downloadable release artifacts — prebuilt binaries are
**not** committed to this repository (see `bin/README.txt`). The optional
WinUI 3 front-end is not built in CI (it needs the Windows App SDK NuGet
package restored locally).

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
├── src/                        core engine, TSF composer, tray app, WinUI 3
├── tests/                      unit / stress / bench + vendored references
├── tools/                      flat-table generators
├── imebench_kit/               3-way benchmark harness (results regenerated locally)
├── demo/                       interactive console demo
├── scripts/                    engine probe programs
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

**KieeKey v1.2.0** là bộ gõ Tiếng Việt cho Windows, được xây dựng dựa trên
dự án **[OpenKey](https://github.com/tuyenvm/OpenKey)** (GPL-3.0) của tác
giả Tuyen Mai. Toàn bộ engine gốc đã được port sang C++ hiện đại, refactor
và hoàn thiện logic: pipeline hook bất đồng bộ với hàng đợi lock-free,
composer TSF không dùng backspace ảo, bảng âm tiết dạng flat tối ưu cache,
cùng bộ test vi mô + đo hiệu năng + đối chiếu sai khác quy mô hàng triệu
trường hợp. Từ v1.1.1, bật/tắt bộ gõ được thực hiện hoàn toàn trong ứng
dụng (trình đơn khay + nút trong Cài đặt) — không còn tổ hợp Ctrl+Shift.
Từ v1.1.2, số 0–9 luôn gõ ra chữ số (hết cảnh gõ số bị thành dấu tiếng
Việt — tùy chọn, mặc định BẬT), hộp thoại Cài đặt có thêm tab **Thông tin**
(giới thiệu ứng dụng, tính năng, hướng dẫn, bản quyền) cùng giao diện mới
gọn gàng, hiện đại hơn.

KieeKey phát hành mã nguồn mở theo **Giấy phép GNU GPLv3** (kế thừa trọn
vẹn từ OpenKey). Thông tin bản quyền của tác giả gốc được giữ lại trong
đầu mỗi file nguồn; mã nguồn tham chiếu của OpenKey 2.0.5 và UniKey được
giữ nguyên văn trong `tests/reference/` với giấy phép gốc của chúng — chi
tiết tại [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
