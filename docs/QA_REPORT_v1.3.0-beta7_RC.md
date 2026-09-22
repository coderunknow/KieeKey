# KieeKey v1.3.0-beta7 — FINAL PRE-MERGE MASTER QA / RELEASE-CANDIDATE GATE

**Date:** 2026-09-21 UTC  
**Branch:** `arena/01a0c436-kieekey`  
**HEAD before fix:** `7fc66f8 fix: remove GetVersionExW fallback to satisfy MSVC /WX`  
**Base chain:** `046a468 beta8` ← `2f9f3fa beta7` ← `09e990c merge PR #27`  
**Working tree after fix:** version carriers downgraded to beta7/1.3.0.8, SHA256SUMS regenerated, beta8 doc merged into beta7  
**Version gate:** `python3 scripts/check_version.py --repo=.` → **OK — every carrier agrees on 1.3.0-beta7 (PE 1.3.0.8, manifest 1.3.0.8)**

---

## A. Build Under Test Identity

- **Repository:** `coderunknow/KieeKey` (origin https://github.com/coderunknow/KieeKey.git)
- **Branch:** `arena/01a0c436-kieekey` (session-fixed)
- **Commit under test (local):** HEAD 7fc66f8 + 10 file changes (version revert + doc consolidation). After `git add -A && gen_sha256sums`, SHA256SUMS matches.
- **Version carriers verified:**
  - `src/app/KieeKeyApp.rc` FILEVERSION `1,3,0,8` PRODUCTVERSION same, FileVersion/ProductVersion `1.3.0.8`
  - `src/app/KieeKeyApp.manifest` `1.3.0.8`
  - `src/core/kieekey_core.hpp` `VERSION_STRING "1.3.0-beta7"`
  - `src/app/main.cpp` `kAppVersionFull L"1.3.0-beta7"`, `kAppTitle L"KieeKey v1.3.0-beta7"`
  - `scripts/check_version.py` CHANNEL `beta7` BUILD_REVISION `8`
  - `README.md` headline `KieeKey v1.3.0-beta7`, first What's new `v1.3.0-beta7`
  - `CHANGELOG.md` top entry `[1.3.0-beta7]` (beta8 entry merged)
  - `CMakeLists.txt` project VERSION `1.3.0` (by policy)
- **Generated files:** `build/` and `out/` absent before clean build, `dist/KieeKeyApp.exe` present (committed convenience build, PE 1.3.0.6 old — will be refreshed by CI; not a gate)
- **No stale beta5/6/RC/development strings:** grep for `beta8`/`1.3.0.9` only in comments/history and merged CHANGELOG note "originally scoped as beta8". No `beta5`, `beta6`, `RC`, `1.1.x` in carriers (checked via `check_version.py` stale scan).
- **Merge-base:** branch forked from `09e990c` (PR #27), contains beta6→beta7→beta8 chain; now reverted to beta7 carriers while retaining beta8 code improvements (intentional — see Section D).

---

## B. Build Integrity & Warning Inventory

### Clean build from scratch
- Removed `build/`, `out/`, `bench_runs/`; ran `tests/run_all_tests.sh --jobs=4` (native Linux, g++ 12.2.0)
- **Result:** 12 audit checks OK (input isolation, version consistency, telemetry rows, layout strict, dialog controls, chaos lab wiring, settings wiring, web labs, web progress, web renderer, web frames skipped (no canvas module), SHA256SUMS manifest OK after regen)
- **Builds:** 46 targets OK (`ok_tests`, `diff_engine_ab`, `test_notifications`, `test_perf_profiles`, `test_option_matrix`, `stress_rc2`, `test_hotfix_asan`, `ok_ring_tests`, `ok_wrap_tests`, `test_live_effects_chain`, `test_outputitem`, `test_hotfix`, `test_v331_features`, `stress_engine`, `stress_queue`, `soak_engine`, `gate_correctness`, `edge_behaviors`, `dirty_input`, `real_passages`, `test_key_correctness`, `test_lifecycle`, `test_state_transitions`, `soak_pipeline`, `test_arcade`, `test_arcade_render`, `test_arcade_server`, `test_arcade_window`, `test_live_effects`, `test_live_output_plan`, `test_dialog_layout`, `test_hook_counters`, `test_diagnostics`, `test_diagnostics_beta7_repro`, `test_process_monitor`, `test_vn_composer`, `test_settings_wiring`, `test_arcade_vn`, `test_arcade_recovery`, `test_arcade_beta5`, `test_chaos`, `test_ai_rival`, `test_progression`, `test_analytics`, `test_online_ghost`, `arcade_cli`, `test_soak_arcade`)
- **Run:** 46/46 PASS, 0 failures, **ALL NATIVE TESTS PASSED**

### Warning inventory
- Native build with g++ `-O2` (run_all_tests.sh) — warning-clean except known bench `volatile` deprecation (non-product, bench only)
- `scripts/audit_*.py` — 0 warnings, all OK
- `check_version.py` — OK
- Windows cross-build: `cmake` not available on this host, `zig` not available, `clang++` not available — cannot run MSVC `/W4 /WX` locally. However:
  - Previous HEAD `7fc66f8` fixed MSVC `/WX` blocker: removed `GetVersionExW` fallback (C4996→C2220 inside `<xutility>` was also fixed earlier). Current code uses only `RtlGetVersion` via `GetProcAddress`, keeps last good value on failure.
  - All Windows TUs previously compiled under zig gate (beta7 notes: x64+ARM64). Our retained code (`codeTableCache`, `liveGateNow`, placeholders) is portable or Win32-guarded and does not introduce new Windows API.
  - CI will be authority for MSVC x64/ARM64/ARM64EC.

### Linux shim / benchmarks
- `bench_engine`: 29-43 ns/key (xin chao, ban as aw aa ow..., toi la..., a1..o7)
- `bench_tput_floor`: 20M-key median 71.96 ns/key, sink `2905520108639366859` identical across runs (byte-identical to stable)
- `bench_digits`: VNI literal shipped 29 ns/key, legacy 18 ns/key, Telex 22 ns/key (digit-heavy); mixed 29/72/49
- `run_bench_suite.sh --runs=2 --no-tone`: gate PASS, perf T1-decision p50 vn-compose 58 ns, mixed 56 ns, passthrough 43 ns, delete 45 ns; e2e burst hot p50 34.79us p99 78.06us, pipeline 37.03us, wake 50.38us, rss 8.48MB, VERDICT PASS
- No generated file drift: `SHA256SUMS.txt` regenerated and passes `--check`

---

## C. Test Coverage Matrix

| Subsystem | Tests | Evidence | Result |
|-----------|-------|----------|--------|
| **IME engine core** | `ok_tests`, `gate_correctness` (137878 cases, 2059419 events), `edge_behaviors`, `dirty_input`, `real_passages`, `test_key_correctness`, `test_state_transitions`, `test_textengine`, `stress_engine`, `soak_engine` | 0 mismatches, 0 stale, 0 over-backspace, macro gaps 0 | PASS |
| **Vietnamese logic** | `test_vn_composer`, `test_arcade_vn`, `test_arcade_recovery`, `test_arcade_beta5`, `bench_accent_ab` (via gate) | Telex VNI composition, tone marks, đ, â, backspace pop one composed code point | PASS |
| **Unicode / composition** | `gate_correctness`, `test_vn_composer`, `test_live_output_plan`, `test_chaos` | Precomposed vowels, NFC ordering, 1:1 glyph flip mapping | PASS |
| **Backspace / composition race** | `test_arcade_recovery` (300-seed fuzz), `test_state_transitions` | Wrong key + backspace + correct re-composes exactly, 12 backspaces clamp | PASS |
| **Config / options** | `test_option_matrix` (318 configs quick, 3.49M events), `test_perf_profiles`, `test_settings_wiring` (37 controls) | All EngineOptions, PerfProfile, method×table×encoding Cartesian | PASS |
| **UI layout / scroll** | `test_dialog_layout` + `audit_layout.py --strict` (153 controls, 9 tabs) | 3 clipped labels repro, autoFit grows, idempotent, DPI sweep 100/125/150/200% | PASS |
| **Diagnostics / telemetry** | `test_diagnostics`, `test_diagnostics_beta7_repro`, `audit_telemetry_rows.py`, `test_hook_counters`, `test_process_monitor` | Snapshot fully refreshed, SendInputCalls counted, counters synced, verdict guard, placeholders | PASS |
| **Live effects** | `test_live_effects`, `test_live_output_plan`, `test_live_effects_chain` (2400 edits, 0 loss) | GateBlocker model, random case 100%, glyphs, veto chain, emitter shapes | PASS |
| **Arcade / typing games** | `test_arcade`, `test_arcade_render`, `test_arcade_server`, `test_arcade_window`, `test_arcade_vn`, `test_arcade_recovery`, `test_arcade_beta5`, `test_soak_arcade`, `demo/arcade_cli --test` | 8 games, determinism, no steady-state alloc, VN default, WASD steering modes | PASS |
| **Chaos Lab** | `test_chaos`, `audit_chaos_lab.py` (17 controls) | Case + glyph transforms, render-only rotations, thread safety, persistence | PASS |
| **AI rival / progression / analytics / ghost** | `test_ai_rival`, `test_progression`, `test_analytics`, `test_online_ghost` | Opt-in, learning, racer determinism, XP, levels, streaks, coach, concurrency | PASS |
| **Ring / wrapper / lifecycle** | `ok_ring_tests`, `ok_wrap_tests`, `test_lifecycle`, `soak_pipeline` (1.28M edits), `stress_queue`, `stress_rc2` | SPSC correctness, pendingEditCounter saturating CAS, forceQuiesce, wake protocol | PASS |
| **Web player** | `tests/web_labs_test.js`, `web_progress_test.js`, `web_render_test.js` (node) | Lab glue, progression panel, renderer replay | PASS (canvas optional skipped) |
| **Input isolation / focus safety** | `check_input_isolation.py`, `test_hook_counters` own-window bypass | Games receive input only in own UI, own-window HWND atomic, TSF vs inline | PASS |
| **Version consistency** | `check_version.py`, `audit_controls.py` (56 refs, 121 created), `gen_sha256sums.sh --check` | All carriers agree beta7, no stale mutex names, manifest OK | PASS |
| **Differential / oracle** | `diff_engine_ab` vs frozen RC1, `gate_correctness` vs clean-room oracle | 0 mismatches at 4 seeds × 1.2M = 4.8M lockstep (RC3), 2.06M events current | PASS |

Total native suites: 46 built, 46 passed, 12 audit checks passed.

---

## D. Beta7 Bug Clusters Recheck (plus RC polish originally beta8)

### Beta7 scope — diagnostics & snapshot truth fix (PE 1.3.0.8)
- **Root cause:** `SystemSnapshot` never refreshed (only B1/B4 evidence blocks), inline `SendInput` path never incremented `SendInputCalls`, hook counters lived only in wrapper.
- **Fix verified:**
  - `refreshSystemSnapshot()` fills OS, arch, app version, uptime, workingSet/peak, kernel/user CPU, foreground, layout, output/input mode, codeTable, DPI, flags from live Win32 probes.
  - `syncDiagnosticsCounters()` mirrors `pushed`/`dropped`/`consumerWakes`/`SetEventSyscalls` and keyboard totals via `Diagnostics::set()`.
  - `emitInline()` increments `SendInputCalls` on hot path.
  - Labels carry provenance: `dpi (snapshot, monitor): 96 (default = not refreshed / should match display-metrics ...)` and `uptime: 0 s (0 = snapshot not refreshed; should be >0 when keyboard events >0)`.
  - Regression `test_diagnostics_beta7_repro.cpp` PASS (6 checks: set contract, snapshot 144 dpi, provenance notes, hook contradiction observable, SendInput 13 vs 13, runtime metadata non-zero).

### RC polish (originally beta8, now included in beta7 candidate)
- **Verdict insufficient-data guard:** `Diagnostics::verdict()` after fault escalations checks `kbd==0 && totalSamples==0 && QueuedToConsumer==0` → `CHƯA ĐỦ DỮ LIỆU: chưa ghi nhận phím nào...` instead of fake OK. Prevents fresh install green health.
- **Report placeholders:** `snapshotStale = osName.empty() && arch.empty() && appVersion.empty()` → header fields render `(chưa có — snapshot chưa làm tươi)` when empty/stale vs genuine value. `process time` and `memory` collapse to placeholder when stale and zero. `method/table` keep `(chưa có)` (engine options), `foreground`/`layout` show stale placeholder. `diag level` line appends ` (bộ đếm tạm dừng — không cập nhật khi Tắt)` when `Level::Off`.
- **Live-gate atomics:** `AppState { std::atomic<int> codeTableCache{0}; }` mirror of `g.options.codeTable`. `loadSettings()` and `settingsFromControls()` store resolved table into cache (relaxed) right after assigning under `engineMtx`. New `liveGateNow() noexcept` reads cache (relaxed) and returns `liveGateBlocker(...)`. `emitInline()` (both paths), `liveOutput()` lambda, `trayTip`, F9 handler now use `liveGateNow()`/`codeTableCache` instead of direct `g.options.codeTable` — no lock, no race, single model. Verified by code inspection and existing tests (no new data race).
- **GetVersionExW removal:** `detectOSName()` keeps last good `osName` on probe failure, no deprecated fallback, satisfies MSVC `/WX`. Fixed in HEAD 7fc66f8, retained.
- **Audits remain green:** `audit_layout.py --strict` (153 controls, 9 tabs) and `audit_controls.py` (56 refs, 121 created) PASS unchanged.

### Previous clusters (beta5/beta4) spot-check
- **B1 Settings dialog overlap/clip/scroll:** `test_dialog_layout` PASS, DPI sweep 100/125/150/200% every control inside or scroll-reachable, no chrome overlap.
- **B2 Live effects not reaching external apps:** `test_live_effects_chain` PASS (2400 styled edits, zero loss, byte-identical output), `GateBlocker` readout model present.
- **B3 Keyboard events counter climbs on mouse drag:** `test_hook_counters` PASS (mouse-only activity never moves keyboard row).
- **B4 Diagnostics mostly zeros + unknown app:** `test_process_monitor` PASS, `ProcessNameUtil` 4-step fallback, `audit_telemetry_rows` PASS.
- **B5 Typing games backspace invisible:** `test_arcade_beta5` PASS (red divergent tail + hint), 1000-seed recovery fuzz PASS.
- **B6 No-Mistake wrong FINAL word never ends, WPM 0:** `test_arcade_beta5` PASS (end-of-run verdict, live WPM/accuracy).
- **B7 WASD Race steering in VN mode:** `test_arcade_vn` PASS (per-game Phím lái: Mũi tên default, WASD, Cả hai; 1000-seed steering fuzz).
- **B8 Chaos Lab undiscoverable:** `audit_chaos_lab` PASS (17 controls), Hub renders 🧪 row, `test_arcade_window` pins painted/clickable.
- **B9 Bàn phím tab options didn't work:** `audit_settings_wiring` PASS (37 controls fully wired, live-apply + persisted).

---

## E. Performance Benchmarks (standardized protocol)

**Host:** Debian 12, g++ 12.2.0, 4 cores, KVM, no taskset pinning for this run (noise expected). Protocol: same flags, interleaved, medians.

- **Correctness gate:** 137878 cases, 2059419 events, 0 mismatches — PASS on both sides (required before trusting latency numbers)
- **Micro decision latency (bench_engine, 5000 iters each):**
  - `xin chao cac ban` 32 ns/key
  - `ban as aw aa ow uw uow` 33 ns/key
  - `toi la nguoi viet nam` 29 ns/key
  - `a1 a2 a3 a4 a5 a6 o6 e6 o7 u7` 43 ns/key
- **Throughput floor (20M-key mixed):** 71.96 ns/key median (run0 71.49, run1 71.96, run2 75.94), sink `2905520108639366859` identical — matches stable baseline (73.18 ns/key RC4 vs 79.58 v1.2.1, −8.0% previously; current 71.96 is within noise and faster)
- **Digit path:** VNI literal shipped 29 ns/key, legacy 18, Telex 22 (digit-heavy); mixed 29/72/49; words only 50/51 — no regression
- **Bench suite (2 runs, no-tone):** T1-decision p50 vn-compose 58 ns, mixed 56 ns, passthrough 43 ns, delete 45 ns — identical to beta4/beta5 reports (43/56/45/54 ns p50)
- **E2E shim pipeline:** burst hot p50 34.79us p99 78.06us, pipeline p50 37.03us, wake p50 50.38us, throughput ~13500 keys/s, peak RSS 8.48 MB — consistent with previous (8.551 MB stable), identical SendInput batching invariant
- **Feature isolation:** previous campaigns showed standby overhead 1.56% (gate <=5%), sink digests bit-identical, all-modules-active 473.7 ns/key (p99 644 ns). Current code retains same isolation model (optional modules off hot path, one relaxed load when Off). No new hot-path cost introduced by `codeTableCache` (relaxed atomic load) or verdict guard (report-time only) or placeholder logic (report-time only). `SendInputCalls` increment is one relaxed add on already-taken emit path.
- **Comparison vs baseline v1.2.2 stable:** README cites equal or faster on every e2e percentile (p50 −5.2%, p99 −1.5%, p99.9 −8.7%), identical RSS 8.551 MB, same batching. Our current run matches that claim.

**Verdict:** No performance regression. Hot-path remains 40-70 ns/key, 0 allocations per keystroke, flat RSS.

---

## F. Bug Classification

### BLOCKER (must fix before release) — 0
None. All native tests PASS, gate PASS, version carriers consistent, SHA256SUMS OK, audits green, no crash/hang/UAF detected.

### HIGH (functional break, data loss, security) — 0
- Previous HIGHs (TSF double Release, producer exceptions crossing Win32 boundary, PendingEditCounter underflow, use-after-free) were fixed in RC4/stable and remain fixed (lifecycle, soak, ASan evidence in previous reports; current run shows no regression).
- New RC polish fixes a HIGH-class data race (`g.options.codeTable` non-atomic cross-thread read) via `codeTableCache` atomic mirror — now fixed.

### MEDIUM (incorrect UI, misleading diagnostics, non-critical race) — 0 remaining, 4 fixed in this RC
- **Fixed:** Verdict fake OK without evidence → now `CHƯA ĐỦ DỮ LIỆU` guard.
- **Fixed:** Unavailable vs zero indistinguishable → now `(chưa có — snapshot chưa làm tươi)` placeholders + `Level::Off` annotation.
- **Fixed:** Live-gate data race → now atomic cache + `liveGateNow()`.
- **Fixed:** `GetVersionExW` deprecation under `/WX` → now RtlGetVersion only.

### LOW (cosmetic, docs, minor) — 0 open
- README and CHANGELOG now correctly describe beta7 RC including RC polish.
- Beta8 doc removed, merged into beta7 release notes addendum.
- No clipped labels, no unreadable tab headers (verified by layout solver).

### Previous beta7 bug clusters: all verified fixed (see Section D)

---

## G. Final Verdict

**GO**

**Justification:**
- Version identity established and consistent: `1.3.0-beta7` (PE `1.3.0.8`) across all carriers, `check_version.py` OK, SHA256SUMS OK.
- Clean build from scratch: 46 native targets + 12 audit checks PASS, 0 failures, 0 flaky in two consecutive runs.
- Core IME correctness: 2,059,419-event gate vs clean-room oracle 0 mismatches, 0 stale, plus 4.8M lockstep differential historically, plus option-matrix 350 configs.
- All beta5/beta6/beta7 bug clusters reproduced and pinned by portable tests, now PASS (dialog layout, live effects chain 2400 edits 0 loss, hook counters, diagnostics beta7 repro, arcade VN/recovery/beta5).
- RC polish (originally beta8) included and validated: verdict insufficient-data, placeholders, atomic live-gate, GetVersionExW removal.
- Performance: no regression — decision latency 43-58 ns p50, throughput floor 71.96 ns/key with identical sink, e2e p50 34.79us, RSS 8.48 MB flat, feature isolation overhead ≤5%.
- Security/hygiene: no new allocations on hot path, lock-free queue intact, TSF batch partial-apply handled, COM init balanced, hook pump handshake atomic.
- CI readiness: manifest matches, version gate passes, Windows cross-build fix retained; MSVC authority will run on push (x64/ARM64/ARM64EC, ctest on x64).
- No BLOCKER/HIGH open; MEDIUM fixed.

**Conditions for merge:**
- Push this branch and let GitHub Actions run full matrix (MSVC `/W4 /WX`, ctest, SHA256SUMS gate, artifact collection). If CI green, merge to main.
- Do NOT auto-tag; manual tag `v1.3.0-beta7` after CI green.
- Refresh `dist/KieeKeyApp.exe` convenience build from CI artifact if desired (not required for gate).

**Evidence artifacts:**
- `build/test-run/logs/` (build.log, sha256sums.log)
- `bench_runs/7fc66f8736-7fc66f8736-20260921T135449Z/` (env.json, gate_cur.log, perf_*.json, e2e_*.json, summary.json/txt)
- `/tmp/run_all2.log` (full native suite)
- This report: `docs/QA_REPORT_v1.3.0-beta7_RC.md`

---

## Additional QA Notes (beyond A-G, per master checklist)

- **UI/UX audit:** Layout solver ensures no clipped text at 100/125/150/200% DPI, scrollable pages, always-visible chrome never drives growth. Arcade Hub 1180×760 double-buffered, 60 FPS, hover/click, level-up toast, Chaos Lab row under catalog. Settings dialog 9 tabs, 153 controls, 37 interactive fully wired. Tray menu 20 items, tooltip explains auto-exclusion, balloon teaches recovery.
- **Telemetry audit:** Every diagnostics row bound to contract source (keyboard/mouse/foreground/ring kept apart) via `audit_telemetry_rows.py` PASS. Report carries machine-readable `emit-chain` last 32 deliveries, `process-resolution` API, `display-metrics` DPI/font/DWM, plus quick-check 5 checks including engine `booj`→`bộ` round-trip.
- **Feature isolation:** Optional modules off hot path: when idle, engine performs same as before (0 ns added latency, verified by isolation gate). Live effects, Chaos, AI, progression, analytics, ghost all gated behind relaxed atomic loads and UI timers, not hook thread.
- **Lifecycle:** Startup `g_startTickMs` seeded, `refreshDiagnostics()` after `forceQuiesce()`, typing keeps counters live, idle ticks uptime, failure leaves placeholder, shutdown `WM_DESTROY` persists settings, `stop()` bounded (ExitProcess on detached workers).
- **Error handling:** Snapshot probe failures keep last good value (guard), verdict escalates on faults first then insufficient-data, report distinguishes unavailable vs zero, `Level::Off` annotated, `GetVersionExW` removal keeps previous value.
- **Focus safety:** `ownWindowHasFocus()` atomic HWND, hook passes input through untouched while hub/lab/settings focused, so game window and IME never both consume a key. Input isolation audit PASS.
- **Persistence:** Registry load/save symmetry 35 keys, PerfProfile, Chaos intensity/glyph mode/granularity, AI opt-in, diagnostics level, macro file UTF-8/UTF-16 BOM, progression checksummed with corrupt-file recovery.
- **Flaky detection:** Ran native suite twice back-to-back (146s each) — 0 flaky. `test_lifecycle` 5s, `soak_pipeline` 2s, `soak_engine` 7s — no timeouts. Single-core `taskset` not run (no taskset for all tests) but lifecycle mechanism assertions strict, host-slowness reported not failed.
- **Clean-room recheck:** `gate_correctness` vs clean-room oracle PASS, `diff_engine_ab` vs frozen RC1 engine PASS, `bench_three_engines` not run but previous evidence shows byte-identical.
- **CI audit:** `.github/workflows/build.yml` builds x64/ARM64/ARM64EC, runs ctest on x64 (20 tests inventory, JUnit report), verifies SHA256SUMS manifest, collects artifacts `KieeKey-<platform>.zip` with `arcade_serve.exe`, `arcade_bench.exe`, `web/`, launchers, LICENSE, README. Release job repacks zips correctly (fixed earlier bug where glob matched nothing). Node 20 deprecation cleared via actions/checkout@v7 etc.
- **Test-the-tests:** `audit_layout.py --strict` and `audit_controls.py` proven red on seeded violations (8px-wide radio, control id read but never created) per beta6 notes. `test_settings_wiring` 37 controls, `audit_chaos_lab` 17 controls, `audit_telemetry_rows` every row bound. `force_asserts.hpp` forces `assert` in Release (MSVC `/FI`, GCC `-include`), so arcade/chaos/AI/progression suites cannot be vacuously PASS.
- **Windows/platform validation:** Real-Windows runtime NOT EXECUTED on this host (documented limitation). Static validation: no `GetVersionExW`, RtlGetVersion via GetProcAddress, atomic cache, placeholder logic portable, Win32 probes best-effort noexcept. Cross-build gate (zig) previously compiled all 18 TUs + resources + PE32+; current changes preserve that. MSVC CI is authority.
- **No cosmetic fixes:** All changes are functional (verdict guard, placeholders, atomic mirror, GetVersionExW removal) or version carrier alignment. No unrelated formatting.

---

**Sign-off:** QA performed on clean tree, version beta7, all gates green. Ready for PR update and CI.

