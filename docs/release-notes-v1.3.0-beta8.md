# KieeKey v1.3.0-beta8 — release notes

**Telemetry presentation polish & live-gate atomics.**
Windows file version **1.3.0.9** · tag `v1.3.0-beta8` · GPL-3.0.

Beta7 fixed the pipeline truth (snapshot refreshed, SendInput counted, hook counters synced). Beta8 audits the *presentation* of that truth: a report with zero evidence should not claim `OK`, an unavailable field should not look like a measured `0`, and the live-effects gate must not read `g.options.codeTable` across threads while the UI writes it.

---

## Root cause — diagnostics was truthful but not reliable

**Report as observation (priority: runtime > source > instrumentation > diagnostics):**

- `verdict: OK — không phát hiện lỗi pipeline` with `keyboard events 0`, `TotalEdit n=0`, `QueuedToConsumer 0` — a fresh install showed a green health check before a single keystroke. The verdict escalated on faults but had no `insufficient-data` guard.
- `app: ` blank, `os: ` blank, `arch: ` blank, `process time: user 0 ms, kernel 0 ms`, `memory: WS 0 kB (peak 0 kB)`, `output: ` blank, `foreground: ` blank, `layout: ` blank — empty/zero was rendered as `0`/blank, indistinguishable from a genuine measurement of zero (e.g. a legit 0 ms at startup). A tester could not tell whether the snapshot was stale.
- `diag level: Basic` vs `Off` was invisible in the report — with `Level::Off`, counters freeze but the report gave no cue that numbers were stale.
- `g.options.codeTable == CodeTable::Unicode` was read in `emitInline()`, `liveOutput()` lambda, `trayTip` and F9 handler while the UI thread wrote `g.options.codeTable` under `engineMtx` — a non-atomic cross-thread read (data race, UB). The UI could show `Unicode` while the emit chain saw non-Unicode, or vice versa.

**Source-of-truth map:**

- Verdict: `Diagnostics::verdict()` over `get(Counter::ProducerExceptions)... histogram(TotalEdit)` — evidence is `keyboardEvents()` + `histogram(TotalEdit).total()` + `QueuedToConsumer`.
- Snapshot freshness: `SystemSnapshot` populated only by `refreshSystemSnapshot()` (Win32). Before first refresh, fields stay `""`/`0`. The report must distinguish `""` (not yet refreshed) from `"0"` (measured zero).
- Live gate: `ok::effects::liveGateBlocker(imeEnabled, excludedApp, liveEffectsEnabled, isUnicode)` — `isUnicode` must be a single coherent view shared by the hook, consumer, F9 and UI readout.

**Reproduce before fixing:**

1. Start KieeKey, keep `Level::Basic`, generate report without typing — beta7 verdict says `OK`; beta8 must say `CHƯA ĐỬ DỮ LIỆU`.
2. Generate report before first `refreshSystemSnapshot()` (or mock empty snapshot) — beta7 shows blank app/os/arch and `0 s`/`WS 0 kB`; beta8 shows `(chưa có — snapshot chưa làm tươi)`.
3. Toggle codeTable Unicode→TCVN3→Unicode rapidly while typing — with race, emit-chain gate and tray tooltip can disagree; with atomic cache they agree.

---

## Fix — only after root cause, preserve semantics, improve labels

**1. Verdict insufficient-data guard (`src/core/Diagnostics.cpp`):**
- After the existing escalations (faults, dropped, sendFailed, p99, reinstalls, barriers), check `kbd==0 && totalSamples==0 && QueuedToConsumer==0` → `CHƯA ĐỦ DỮ LIỆU: chưa ghi nhận phím nào — hãy gõ thử trong ứng dụng ngoài (ví dụ Notepad) rồi xuất lại báo cáo`. This gates `OK` on evidence, after all error paths, so real faults still dominate.

**2. Report placeholders (`Diagnostics::report`):**
- `snapshotStale = osName.empty() && arch.empty() && appVersion.empty()`. Header fields render `"(chưa có — snapshot chưa làm tươi)"` when empty/stale vs genuine value. `process time` and `memory` collapse to placeholder when stale and zero; otherwise show measured numbers (including legitimate zero after refresh).
- `method/table` keep `"(chưa có)"` (engine options, not snapshot freshness); `foreground`/`layout` show stale placeholder.
- `diag level` line appends ` (bộ đếm tạm dừng — không cập nhật khi Tắt)` when `Level::Off`, so frozen counters are explained.

**3. Live-gate atomics (`src/app/main.cpp`):**
- `AppState { std::atomic<int> codeTableCache{0}; // mirror of g.options.codeTable }` — `0 == CodeTable::Unicode`.
- `loadSettings()` and `settingsFromControls()` store the resolved `g.options.codeTable` into `codeTableCache` (relaxed) right after assigning under `engineMtx`.
- New `liveGateNow() noexcept` — reads `codeTableCache` (relaxed), maps to `bool isUnicode = cache==0`, returns `liveGateBlocker(g.imeEnabled, g.excludedApp, g.liveEffects.enabled(), isUnicode)`.
- `emitInline()` (both paths), `liveOutput()` lambda, `trayTip` and F9 handler now use `liveGateNow()`/`codeTableCache` instead of `g.options.codeTable` directly — no lock, no race, single model.

**4. Audits remain green:**
- `python scripts/audit_layout.py --strict` (153 controls, 9 tabs) and `python scripts/audit_controls.py` (56 refs, 121 created) pass unchanged — presentation change is diagnostics-only.

Platform honesty: verdict/report logic is portable and covered by native suite; `codeTableCache` and snapshot probes are Win32-only and validated under zig/MSVC full-TU gates.

---

## Validation — startup / typing / idle / failure / shutdown

- **Startup**: `g.codeTableCache` initialized to Unicode (matches `EngineOptions` default); `loadSettings` syncs after first registry read; first report shows `CHƯA ĐỦ DỮ LIỆU` until keys arrive, then flips to `OK` with p99.
- **Typing**: rapid table switches stay coherent — hook `emitInline` and UI readout share atomic view; no torn enum read.
- **Idle**: `Level::Off` report shows `bộ đếm tạm dừng` and stale placeholders remain honest; verdict still needs evidence even if level is Basic.
- **Failure**: any snapshot probe failure leaves placeholder; verdict still escalates on faults first, insufficient-data last.
- **Shutdown**: `WM_DESTROY` persists settings; atomic cache is process-lifetime, no teardown race.

## Version carriers — 1.3.0-beta8 (PE 1.3.0.9)

- `src/app/main.cpp` `kAppVersionFull` / `kAppTitle` → `1.3.0-beta8`
- `src/app/KieeKeyApp.rc` FILEVERSION/PRODUCTVERSION → `1,3,0,9` / `1.3.0.9`
- `src/app/KieeKeyApp.manifest` → `1.3.0.9`
- `src/core/kieekey_core.hpp` `OPENKEY_KIEEKEY_VERSION_STRING` → `1.3.0-beta8`
- `scripts/check_version.py` `CHANNEL beta8 BUILD_REVISION 9`
- `README.md` / `CHANGELOG.md` / `docs/release-notes-v1.3.0-beta8.md` beta8 entries
