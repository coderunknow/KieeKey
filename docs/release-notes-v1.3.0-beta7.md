# KieeKey v1.3.0-beta7 — release notes

**Diagnostics & snapshot truth fix.**
Windows file version **1.3.0.8** · tag `v1.3.0-beta7` · GPL-3.0.

Beta6's exported diagnostics report contradicted itself on the tester's view and on static code reading.
This release audits the report as an observation, not a source of truth, and fixes the pipeline that filled it.

---

## Root cause — diagnostics was a stale snapshot, pipeline counters were detached

**Report as observation (priority: runtime > source > instrumentation > diagnostics):**

- `dpi: 96` in `SystemSnapshot` vs `display-metrics dpi=144` — 96 is the struct's default initializer, not a probe.
  `refreshEvidenceContext()` updated only `ProcessResolution` + `DisplayMetrics`, never `SystemSnapshot.dpi`.
- `SendInputCalls: 0` vs 13 deliveries in the `emit-chain` trace (`channel=SendInput`) — the counter was incremented only on the deferred consumer path (`InlineEdit` handler) and on `TsfComposer` fallback, never on the hot inline `emitInline()` path where every Vietnamese composition actually injects.
- `hook: CHƯA cài` vs `keyboard events >0`, `queue.pushed 0` vs keyboard events, `uptime 0 s`, `workingSet 0 KB`, `osName`/`arch`/`appVersion`/`foregroundApp`/`keyboardLayout` empty, `cpu 0.0 %` — the entire `SystemSnapshot` was zero-initialized and never populated before `Diagnostics::report()`.

**Source-of-truth map (where each value lives):**

- Hook liveness: `g.hook.running()` (ModernKeyHook/Win32Wrapper), not `SystemSnapshot.hookInstalled` unless refreshed.
- Per-source event counts: `HookCounters` (`keyDown`/`keyUp`/`mouseButton`/`foregroundChanged`) and `Win32Wrapper::pushed()`/`dropped()` + `HookCounters::consumerWakeups`/`setEventSyscalls` — the report's `Counter::HookQueuePushed` etc. were never synced from those live atomics.
- `SendInput` injections: `InlineEmitter::sendEdit()` (chunked `SendInput`) — observable via `emitTrace` at count 13, not via `Counter::SendInputCalls` which stayed 0.
- Runtime metadata: `GetTickCount64` delta from `g_startTickMs`, `GetProcessMemoryInfo`, `GetProcessTimes`, `GetNativeSystemInfo`, `GetKeyboardLayout`/`LCIDToLocaleName`, `ProcessMonitor::snapshot()`, `g.outputMode`/`g.options` — none were copied into the snapshot.
- Display truth: `windowDpi()` / `systemDpiOrFallback()` / `DisplayMetrics` — the report's `dpi` line showed only the stale snapshot value without provenance.

**Reproduce before fixing (controlled Windows repro plan):**

1. Start KieeKey, keep default `Level::Basic` (one relaxed load per key).
2. Verify hook: tray tooltip shows mode, diagnostics tab `hookReinstallCount` stays 0.
3. Type in Notepad (`as` → `á`, `booj` → `bộ`) and in a browser Office app (TSF path) — verify composition, then toggle IME off and verify pass-through.
4. Toggle Vietnamese IME, generate diagnostics via `Xuất báo cáo` / `Sao chép báo cáo` — compare counters vs hook status, `SendInput` vs emit-chain, `dpi` rows, `uptime`/`workingSet` vs Task Manager.
5. Without fix, report shows `dpi 96` while `display-metrics dpi=144`, `SendInputCalls 0` while `emit-chain` has 13 `SendInput` lines, and all runtime snapshot fields at `0`.

---

## Fix — only after root cause, preserve semantics, improve labels

**1. `Diagnostics::set(Counter, uint64_t)` (portable):**
- Added `void set(Counter, uint64_t) noexcept` to `Diagnostics.hpp`/`Diagnostics.cpp` — atomic store, so `main.cpp` can rebase counters from live sources without a read-modify-write race.

**2. `SystemSnapshot` full refresh (`src/app/main.cpp`, Win32):**
- `g_startTickMs` captured in `wWinMain` before any snapshot; `refreshSystemSnapshot()` fills:
  OS name (RtlGetVersion + ProductName), arch (GetNativeSystemInfo + IsWow64Process2 for ARM64EC), app version (kAppVersionFull + PE FileVersion via GetFileVersionInfo), uptime (GetTickCount64 delta), workingSet/peak (GetProcessMemoryInfo), kernel/user time + cpu% (GetProcessTimes), foreground app + policy hint (ProcessMonitor::snapshot + g.fgUseTsf_/g.fgExcluded_), keyboardLayout (HKL hex + LCIDToLocaleName), outputMode, inputMethod, codeTable, DPI (windowDpi / systemDpiOrFallback), and flags (imeEnabled/hookInstalled/fgHookInstalled/liveEffects/excluded).
- `refreshEvidenceContext()` still refreshes B1/B4 evidence blocks; `refreshSystemSnapshot()` calls it so the two never disagree.
- Best-effort noexcept, exception-swallowed: evidence never takes the IME down.

**3. Counter sync (`syncDiagnosticsCounters()`):**
- Mirrors `Win32Wrapper::pushed()` → `HookQueuePushed`, `dropped()` → `HookQueueDropped`, `HookCounters::consumerWakeups` → `ConsumerWakes`, `setEventSyscalls` → `SetEventSyscalls`, and rebases `KeyDown`/`KeyUp` total to match `HookCounters::keyboardEvents()` (the labeled source-of-truth, not the ring counter). `SendInputCalls` is counted on the hot inline path.

**4. Hot-path wiring:**
- `emitInline()` increments `Counter::SendInputCalls` on any backspace/text injection (one relaxed add, Basic-gated implicitly via the emitter path).
- `onHookEvent()` increments `Counter::MouseEvents` and `Counter::ForegroundChanges` for non-keyboard sources (Basic-gated).
- `flushEditBatch()` fallback already counted via emitter; deferred `InlineEdit` path already counted `SendInputCalls` + histogram — preserved.

**5. UI / export refresh (`refreshDiagnostics()`):**
- `refreshDiagnostics()` = `refreshSystemSnapshot()` + `syncDiagnosticsCounters()` + `refreshEvidenceContext()`.
- Called on startup after `forceQuiesce()`, before every `Xuất báo cáo` and `Sao chép báo cáo`, and before quick-check (`IDC_BTN_DIAG_RUN`).
- Report labels now carry provenance:
  `dpi (snapshot, monitor): 96 (default = not refreshed / should match display-metrics dpi=96 if refreshed)` for the default,
  `dpi (snapshot, monitor): 144, systemDpi requested=144 (if per-monitor DPI-aware, systemDpi may be stale; ...)`
  and `uptime: 0 s (0 = snapshot not refreshed; should be >0 when keyboard events >0)`.

**6. Regression:**
- `tests/test_diagnostics_beta7_repro.cpp` — portable repro of every beta6 contradiction (snapshot defaults vs 144, SendInput 0 vs 13 emits, hook mismatch, uptime/DPI provenance, workingSet zeros, counter set contract).

Platform honesty: snapshot/DPI/memory/cpu/foreground probes are Win32-only and validated under the zig full-TU gate and MSVC CI; the `set()` contract and report labels are portable and gated in the native suite.

---

## Validation — startup / typing / idle / failure / shutdown

- **Startup**: `g_startTickMs` seeded, `refreshDiagnostics()` after `forceQuiesce()` — first report already non-zero.
- **Typing**: `emitInline()` + `onHookEvent()` keep counters live; `syncDiagnosticsCounters()` rebases ring/wake counters before export so idle vs burst disagree by at most one batch.
- **Idle**: `SystemSnapshot::uptimeMs` ticks via `GetTickCount64`; `cpuPercentSinceStart` guarded against overflow.
- **Failure**: any probe failure leaves field at last good value; all refresh paths are `noexcept` + try/catch.
- **Shutdown**: `WM_DESTROY`/`WM_ENDSESSION` persist settings; `stop()` is bounded (ExitProcess on detached workers).

## Addendum — RC polish included in this beta7 candidate (originally beta8 scope)

Beta7 fixed the pipeline truth (snapshot refreshed, SendInput counted, hook counters synced). The same candidate audits the *presentation* of that truth: a report with zero evidence should not claim `OK`, an unavailable field should not look like a measured `0`, and the live-effects gate must not read `g.options.codeTable` across threads while the UI writes it. Windows additionally fixes `GetVersionExW` deprecation under `/WX`.

**Root cause — diagnostics was truthful but not reliable:**

- `verdict: OK` with `keyboard events 0`, `TotalEdit n=0`, `QueuedToConsumer 0` — fresh install showed green health before a single keystroke.
- `app: ` blank, `os: ` blank, `arch: ` blank, `process time: user 0 ms, kernel 0 ms`, `memory: WS 0 kB` — empty/zero rendered as `0`/blank, indistinguishable from genuine zero.
- `diag level: Basic` vs `Off` invisible — with `Level::Off`, counters freeze but report gave no cue.
- `g.options.codeTable == CodeTable::Unicode` read in `emitInline()`, `liveOutput()` lambda, `trayTip` and F9 handler while UI thread wrote it under `engineMtx` — non-atomic cross-thread read (data race, UB).
- `GetVersionExW` fallback in `detectOSName()` — deprecated, fails `/W4 /WX` on MSVC; `RtlGetVersion` path already succeeds.

**Fix:**

1. Verdict insufficient-data guard (`Diagnostics.cpp`): after existing escalations, check `kbd==0 && totalSamples==0 && QueuedToConsumer==0` → `CHƯA ĐỦ DỮ LIỆU: chưa ghi nhận phím nào — hãy gõ thử trong ứng dụng ngoài (ví dụ Notepad) rồi xuất lại báo cáo`. Gates `OK` on evidence, after all error paths.
2. Report placeholders: `snapshotStale = osName.empty() && arch.empty() && appVersion.empty()` → header fields render `(chưa có — snapshot chưa làm tươi)` when stale; `process time` and `memory` collapse to placeholder when stale and zero; `diag level` line appends ` (bộ đếm tạm dừng — không cập nhật khi Tắt)` when `Level::Off`.
3. Live-gate atomics (`main.cpp`): `AppState { std::atomic<int> codeTableCache{0}; }` mirror of `g.options.codeTable`; `loadSettings()` and `settingsFromControls()` store resolved table into cache; new `liveGateNow() noexcept` reads cache (relaxed) and returns `liveGateBlocker(g.imeEnabled, g.excludedApp, g.liveEffects.enabled(), isUnicode)`; `emitInline()`, `liveOutput()` lambda, `trayTip`, F9 handler now use `liveGateNow()`/`codeTableCache`.
4. `GetVersionExW` removal: `detectOSName()` keeps last good `osName` on probe failure, no deprecated fallback.

**Validation:** startup cache initialized to Unicode, first report `CHƯA ĐỦ DỮ LIỆU` until keys, rapid table switches stay coherent, `Level::Off` annotated, snapshot failure leaves placeholder, verdict still escalates on faults first.

## Version carriers — 1.3.0-beta7 (PE 1.3.0.8)

- `src/app/main.cpp` `kAppVersionFull` / `kAppTitle` → `1.3.0-beta7`
- `src/app/KieeKeyApp.rc` FILEVERSION/PRODUCTVERSION → `1,3,0,8` / `1.3.0.8`
- `src/app/KieeKeyApp.manifest` → `1.3.0.8`
- `src/core/kieekey_core.hpp` `OPENKEY_KIEEKEY_VERSION_STRING` → `1.3.0-beta7`
- `scripts/check_version.py` `CHANNEL beta7 BUILD_REVISION 8`
- `README.md` / `CHANGELOG.md` beta7 entries

