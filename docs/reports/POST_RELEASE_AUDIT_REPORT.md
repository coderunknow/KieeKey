# KieeKey v1.1.0 — Post-Release Audit Report

Date: 2026-09-02 · Scope: full-source audit of the v1.1.0 release drop
(engine, hooks, TSF composer, tray app, tests) for **edge cases,
correctness, consistency, stability and speed**, with every fix verified by
the complete regression battery. **The version stays 1.1.0** (per the
release owner's instruction); the fixes below amend the 1.1.0 source drop
in place.

## Methodology

* Full read-through of `src/core` (~5,000 LOC), `src/tsf`, `src/app`
  (2,300 LOC) plus the resource scripts and version surfaces.
* Machine verification on Linux: the shipped shim-based suites
  (`ok_tests`, `ok_ring_tests`, `ok_wrap_tests`), the engine suites
  (`test_hotfix` 57 checks, `test_v331_features`, `edge_behaviors`
  3×1440 prefix determinism, `real_passages`, `dirty_input`,
  `test_outputitem`), the stress/soak suites, the **mega differential**
  (`run_mega_bench.sh --fast`: engine vs clean-room oracle vs the real
  vendored OpenKey 2.0.5, 3.29 M events) and the three-engine benchmark
  (KieeKey vs OpenKey 2.0.5 vs UniKey UKEngine, 2 M keys/engine).
* Cross-build verification: full MinGW-w64 GCC 16.2 static build of
  `KieeKeyApp.exe` (x64) through the shipped CMake + toolchain file.
* Targeted differential probes for every suspected defect (reproducers
  reduced to minimal key streams and compared against the REAL 2.0.5
  engine as ground truth).

## Headline result

The shipped v1.1.0 sources contained **one critical engine regression
family and one latent high-severity TSF defect**, both verified by the
project's own differential harness (the frozen `docs/reports/MEGA_BENCH_
REPORT.md` claims 0 mismatches over 5.97 M cases, but the shipped 1.1.0
tree measures **2,232 mismatches in the 1 %-budget fast run** — the
regression was introduced by the v1.1.0 macro bookkeeping changes after
that report was frozen). All findings below are fixed and the full battery
now ends at **`VERDICT: PASS (0 text divergences)`**.

## Correctness — fixed (engine)

1. **Macro re-expansion regression (suite 10: 2,222/2,232 mismatches).**
   `spaceBranch`'s ReplaceMacro kept `hasHandledMacro_` armed forever:
   after the v1.1.0 D3 change (`spaceCount_ = 0` — the space is consumed
   by the expansion), the legacy "next letter after a tracked space
   restarts the session" trigger never fires, so the flag was never
   re-armed. Typing the same abbreviation twice produced
   `"xl xl " → "xin lỗixl "` instead of `"xin lỗi xin lỗi "`
   (ground truth: the real 2.0.5 engine expands both). Fix: reset the full
   word state at the expansion (mirroring the word-break tail) and re-arm
   the flag when the next word's first letter arrives
   (`mainKeyBranch` fresh-word guard).
2. **Stale-buffer composition after a macro expansion.** The same code
   path left `index_`/`stateIndex_`/`longWordHelper_` at their
   pre-expansion values, so the word typed after an expansion composed
   onto the phantom raw keys ("xl as" produced `xin lỗias` instead of
   `xin lỗiá`). Fixed together with (1). `macroExpandLen_` is now
   dropped when new text follows an expansion (backspaces then delete
   the NEW text, not the committed expansion).
3. **`handleModernMark` rule 3.2 activation regression (suites 4/11/12).**
   v1.1.0 "fixed" 2.0.5's dead `ia/ya/ua` rule by activating it; the
   activation forced the tone mark onto the FIRST vowel of every `U+A`
   group, diverging from both the real 2.0.5 engine and the clean-room
   oracle exactly where the dead code could never fire:
   `ua` + end consonant (kVowelCombine `{1,U,A}` keeps the mark on the
   SECOND vowel — the quán family) and 3-vowel `uai…` groups. Fix: the
   branch stays dead exactly as 2.0.5 shipped it; rules 4/7 already
   produce every documented placement ("mía", "mựa", "giá", "quá").
4. **Oracle (test-side spec) defects mirroring the same family.**
   `vi_oracle.hpp` carried 2.0.5's `hasHandledMacro` leak (the
   spelling-restore path stayed silently gated for the word after a
   comma/enter macro expansion — "uwowng,duwsngkhos " kept `dứngkhos`
   where the engine, matching 2.0.5's own behavior for the same stream
   without the macro, restores the raw keys) and lacked the v1.1.0
   break-key state reset. Both mirrored to the engine's fixed semantics
   (the oracle header explicitly allows "defects in either").

## Stability — fixed (Windows app)

5. **`ProcessMonitor::start()` failure ⇒ `std::terminate` at exit**
   (critical): a failed `SetWinEventHook` left the pump thread joinable
   with `running_` already false, so `stop()` early-returned and never
   joined — `~ProcessMonitor()` on a joinable `std::thread` aborts the
   process. Fixed with a bounded join/detach mirroring `stop()`.
6. **`TsfComposer::textBeforeCaret()` dangling-stack retry** (high): the
   `TF_ES_ASYNCDONTCARE` retry queued a session whose `resultOut_`
   pointed at a returned frame's local and whose `ctx_` could be released
   by `detach()`/`onForegroundChanged()` before TSF ran it
   (stack-use-after-return + COM use-after-free). The retry could never
   produce a usable result (the function returned false unconditionally
   after it) — removed.
7. **COM init imbalance in `TsfComposer`**: `attach()` failure paths
   called `CoUninitialize()` even after `RPC_E_CHANGED_MODE` (no
   reference taken — unbalancing the real owner), and `detach()` never
   balanced its own reference (one leak per attach/detach cycle). Fixed
   with an `ownsComInit_` member (S_OK/S_FALSE own; RPC_E_CHANGED_MODE
   does not).
8. **Singleton takeover killed a healthy starting instance**: a second
   launch inside the first instance's startup window (mutex held,
   `KieeKeyMain` not yet created, PID already published) treated
   "window not found" as "zombie" and `TerminateProcess`'d it. Fixed
   with a 3 s find-retry grace before concluding unresponsive.
9. **`WM_ENDSESSION` ignored `wParam`**: a *canceled* logoff/shutdown
   (another app vetoed) permanently stopped the hook and monitor while
   the session continued — "KieeKey suddenly stopped typing". Fixed.
10. **macros.txt write under `engineMtx`**: the settings Apply path did
    `SHGetKnownFolderPath` + `CreateDirectory` + `std::ofstream` while
    holding the same lock the LL-hook keystroke path takes; a synced/
    AV-scanned write stalled typing (past the hook timeout Windows
    silently unhooks). The table swap stays under the lock; the file
    write moved out.
11. **`ModernKeyHook::start()` failure left `running_` set** — a retry
    reported success with no hooks. Now rolls back via `stop()`.
    A restart after a stuck-detach is also refused (a detached worker
    still owns the SPSC ring; two consumers corrupt it silently).
12. **QPC µs clock overflow** in the consumer's adaptive-gap window:
    `qpcNow() * 1e6` wrapped after ~9–21 days uptime. Reordered to
    divide-then-multiply (exact for ~584 years).
13. **Modifier tracker re-seed after hook self-heal**: `reinstallHooksOn-
    Pump()` recovered the hooks but left `modifierBits_` frozen at its
    dead-window value (a Shift/Ctrl released while the hook was dead
    stayed "held"). Now re-seeds from the async table.
14. **`CtrlShiftChord` half-chord leak**: a non-modifier key between the
    two modifier presses (`Ctrl↓, S↓, Shift↓, Shift↑`) stayed "Clean"
    and fired Toggle. Now dirties on a non-modifier key-down while
    EITHER modifier is held.
15. **`applyDelta` returned `S_OK` with zero selections** — commits
    reported success while nothing was applied (keystroke silently
    lost, no SendInput fallback). Now returns `E_FAIL`.
16. **`Win32RAII` latent traps**: `FindHandle` used `nullptr` as its
    invalid value (FindFirstFile fails with `INVALID_HANDLE_VALUE` —
    a failed find read as valid); replaced with a dedicated correct
    wrapper. The `Win32Handle` move-assign used `&other`, which the
    out-param `operator&` overload hijacks — now `std::addressof`
    (the template's move-assign had never been instantiated, hiding it).
17. **App-level hygiene**: tray menu now posts the KB135880 `WM_NULL`
    after `TrackPopupMenu`; the foreground-change tooltip refresh no
    longer routes through `WM_APP_TOGGLE` (which persisted 13 registry
    values on every Alt-Tab — a tooltip-only handler was added); the
    settings font is cached PER DPI (mixed-DPI dialogs no longer clip);
    `fgProbePending_` is cleared when the probe runs; the macro editor
    is seeded from the raw `macros.txt` content so user comments
    survive an Open→OK round-trip; `std::ifstream/ofstream` take
    `.c_str()` (MSVC-only `wstring` overload removed — the file now
    compiles with the project's own MinGW path, not just MSVC).

## Consistency — verified

Every product-visible version surface re-checked after the fixes:
CMake `project()` 1.1.0, `.rc` `FILEVERSION`/`PRODUCTVERSION 1,1,0,0` +
string values `1.1.0.0`, manifest `1.1.0.0`, singleton mutex
`KieeKey_1.1.0_Singleton`, wake event, balloon/window titles, banners,
`kieekey_core.hpp` macros — **all consistent at 1.1.0, unchanged**.
The internal `v3.x` comment markers are historical lineage annotations
(documented in the changelog's versioning note), not release versions.

## Verification matrix (all re-run after the fixes)

| Suite | Result |
|---|---|
| ok_tests (golden vectors) | ALL PASSED |
| test_hotfix (57 checks incl. chord) | ALL PASSED (failures: 0) |
| test_v331_features | ALL PASSED |
| edge_behaviors (3-way, every prefix) | 1440 / 1440 |
| real_passages | 15/15 intended text, pure-VN round-trip 10/10 |
| dirty_input | all families pass |
| stress_engine / soak_engine / test_outputitem | PASSED (zero heap allocs/key, flat RSS) |
| stress_queue (SPSC) | 346 M ops/s, ALL PASSED |
| ok_wrap_tests / ok_ring_tests | ALL PASSED |
| mega differential (fast: 3.29 M events) | **VERDICT: PASS — 0 divergences** |
| three-engine bench | see below |

## Speed — measured (Linux host, 2 M keys/engine)

| Engine | mean ns/key | p50 | p90 | p99 |
|---|---|---|---|---|
| KieeKey v1.1.0 (fixed) | **216.6** | 198 | 272 | 546 |
| OpenKey 2.0.5 (legacy) | 257.4 | 209 | 381 | 821 |
| UniKey 4.x UKEngine | 121.6 | 114 | 142 | 188 |

(UniKey's tighter numbers reflect a smaller feature surface — no
spelling-restore bookkeeping; the stress suite confirms KieeKey's hot
path remains allocation-free.) E2E pipeline through the real ring +
ordering barrier + emitter shim: p50 10.7 µs, p99 54 µs, p99.9 83.5 µs,
peak RSS 13.9 MB. The Windows `KieeKeyApp.exe` is a fully static
MinGW-w64 build (no runtime DLLs beyond the system set: user32, gdi32,
shell32, ole32, comctl32, advapi32, kernel32, winmm, dwmapi, msvcrt).

## Known / accepted (documented, not changed)

* The `v3.x` lineage markers in comments are historical (changelog
  versioning note).
* Reference engines' own defects (2.0.5's `checkForStandaloneChar` OOB
  read, UniKey's 128-word buffer limit) are documented in
  `THREE_ENGINE_BENCH_REPORT.md` and deliberately left pristine.
* The full-budget mega run (5.97 M cases) takes hours; the fast CI gate
  (1 % budgets, the same code paths) is the executed verification here,
  exactly as CI runs it.
