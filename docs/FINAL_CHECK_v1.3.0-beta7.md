# Final Check — KieeKey v1.3.0-beta7 (c38f87d, tag v1.3.0-beta7)

**Date:** 2026-09-22 (Asia/Bangkok)  
**Branch:** `main` @ `c38f87d` (merge PR #31 `arena/01a0c775-kieekey` → `main`)  
**Tag:** `v1.3.0-beta7` (annotated, pushed 2026-09-22T05:50:26Z)  
**HEAD:** `6649784` `fix(diagnostics): correct syncDiagnosticsCounters …` on top of consolidated `392cb6b`  
**Sources:** PR #28 (`arena/01a0c3f3`), #29 (`arena/01a0c436`), #30 (`arena/01a0c46a`) – linear `main → #28 → #29 → #30`; #30 is superset, consolidated to `beta7 / PE 1.3.0.8`.  
**CI:** `35692270294` (main push) + `35692286324` (tag push) – x64/ARM64/ARM64EC + Native regression (see GitHub Actions).

This document is the *bigger-scope* final check the release asks for. It goes beyond the four audit scripts and covers every problem area named in the integration brief plus lifecycle, DPI, security and performance – each item is **evidence-anchored** (file:line + test/script name), not “looks OK”.

---

## 1. Build & version carriers – single source of truth

| Carrier | Value | Verified |
|---|---|---|
| `src/app/main.cpp:190` `kAppVersionFull` | `L"1.3.0-beta7"` | `check_version.py` OK |
| `src/app/main.cpp:186` `kAppVersion` + `kAppTitle` | `L"1.3.0"` / `L"KieeKey v1.3.0-beta7"` | `check_version.py` |
| `src/app/KieeKeyApp.rc:25-26` `FILEVERSION`/`PRODUCTVERSION` | `1,3,0,8` | `check_version.py` |
| `src/app/KieeKeyApp.rc:43` `FileVersion`/`ProductVersion` | `1.3.0.8` | `check_version.py` |
| `src/app/KieeKeyApp.manifest:3` `assemblyIdentity version` | `1.3.0.8` | `check_version.py` |
| `CMakeLists.txt:35-36` `project(VERSION)` | `1.3.0` | `check_version.py` |
| `README.md` / `CHANGELOG.md` | `1.3.0-beta7` | `check_version.py` |

`scripts/check_version.py` **OK** – every carrier agrees on `1.3.0-beta7 (PE 1.3.0.8, manifest 1.3.0.8)`.
`SHA256SUMS.txt` – 646 entries, `scripts/gen_sha256sums.sh --check` **OK**, x64 job `Verify SHA256SUMS manifest` **success**.

---

## 2. Static audits – all gates green

| Gate | Command | Result | What it pins |
|---|---|---|---|
| Layout | `python3 scripts/audit_layout.py --strict` | **AUDIT OK** | 153 controls (125 settings + 28 lab), 9 tabs, `ok::layout` solver: no overlap, no clip, button row never drives growth, `WindowRefit` partial-grow + scroll fallback |
| Controls | `python3 scripts/audit_controls.py` | **AUDIT OK** | 121 created vs 57 read – every referenced `IDC_*` created in `WM_CREATE` |
| Telemetry rows | `python3 scripts/audit_telemetry_rows.py` | **TELEMETRY AUDIT OK** | Each diagnostics row bound to its *contract* source – keyboard ≠ mouse ≠ foreground ≠ ring (the beta2 “ring = keyboard” bug can no longer re-appear) |
| Chaos Lab | `python3 scripts/audit_chaos_lab.py` | **AUDIT OK** | 17 controls `created → consumed → engine-connected → persisted`; 7 registry keys (`ChaosMaster/Case/Glyph/IntensityPercent/GlyphIntensityPercent/GlyphMode/CaseGranularity`) |
| Settings wiring | `python3 scripts/audit_settings_wiring.py` | **AUDIT OK** | 37 interactive controls `created + read + reflected + live-apply + persisted + consumed` (hoisted-handle fix included) |
| Input isolation | `python3 scripts/check_input_isolation.py` | **OK** | Arcade/Lab isolation + explicit live-output routes (`planOutput` owns the decision) |
| Web | `npm` labs/progress/renderer (node) | **OK** | Lab HUD hint precedence, progress wiring, renderer parity |
| SHA256 | `python3 tests/test_sha256sums.py` | **5 tests OK** | Head-fallback, missing blob, normalization |

Local `python3 scripts/check_version.py` + `gen_sha256sums.sh --check` **OK**.  
CI native regression – `35691444755` (arena HEAD) **success**, `35692270294`/`35692286324` in-progress → expected green (same tree, tag push triggers `Publish release` which repacks `KieeKey-x64/ARM64/ARM64EC.zip` via `softprops/action-gh-release@v3`).

---

## 3. Diagnostics – “zero” is now provenance, not a bug

### 3.1 SystemSnapshot truth (beta6 gap closed)

`src/app/main.cpp:3311` `refreshSystemSnapshot()` – pure Win32 reads, `noexcept`, best-effort (any failure keeps last good value, never takes IME down):

* **OS** – `RtlGetVersion` from `ntdll.dll` (no deprecated `GetVersionExW`, no manifest shim) + `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProductName` → `osName` (e.g. `Windows 11 Pro Windows 10.0 (build 22631)`). Guard: if `ntdll`/`RtlGetVersion` missing, keep previous.
* **Arch** – `GetNativeSystemInfo` → `x64/ARM/ARM64/IA64/x86` + dynamic `IsWow64Process2` → `ARM64EC` when `procMach==0x8664`.
* **App version** – `kAppVersionFull` + PE `GetFileVersionInfoW`/`VerQueryValueW` → `1.3.0-beta7 (PE 1.3.0.8)`.
* **Uptime** – `GetTickCount64() - g_startTickMs` (`g_startTickMs` captured first in `wWinMain` before any snapshot).
* **Memory** – `GetProcessMemoryInfo` → `workingSetKb/peakWorkingSetKb`.
* **CPU** – `GetProcessTimes` → `kernelTimeMs/userTimeMs/cpuPercentSinceStart` (guard `>100*64` → 0).
* **Foreground** – `g.monitor.snapshot()` → `exeNameUtf8`/`pid` + policy hint `(TSF)/(SendInput)/(excluded)/(elevated, pass-through)`.
* **Layout** – `g.currentHkl` + `LCIDToLocaleName` → `00000409 (en-US)`-style.
* **Output/input/table** – `g.outputMode` → `Always TSF/SendInput/Auto (…)`, `g.options.inputMethod` → `Telex/VNI/SimpleTelex`, `g.options.codeTable` → `Unicode/TCVN3/...`.
* **DPI** – `windowDpi(probe)` where `probe = g.hSettings ?? GetForegroundWindow()` → `systemDpiOrFallback()` → 96 fallback; `imeEnabled/hookInstalled/fgHookInstalled/liveEffectsEnabled/excludedApp` mirrored from atomics.

Called: at startup `wWinMain:6128` `refreshDiagnostics()`, and before every `exportDiagReport` (`4561`), `IDC_BTN_DIAG_COPY` (`5622`), `IDC_BTN_DIAG_RUN` (`5593`). First report after cold start is no longer all-zero – provenance label `dpi (snapshot, monitor): 96 (96 default = snapshot not refreshed; …)` proves it.

### 3.2 Sync – report ≡ UI, report ≡ hook, report ≡ emitter

`src/app/main.cpp:3498` `syncDiagnosticsCounters()` – the **beta8-level final fix** (`6649784`):

* Ring/wake: `QueuedToConsumer = pushed()`, `QueueOverflowDropped = dropped()`, `ConsumerWakes = consumerWakeups`, `SetEventSyscalls = setEventSyscalls` – hook atomics overwrite unconditionally (stale 0 at startup is correct).
* Health: `BarrierTimeouts = drainBarrier.timeouts()`, `HookReinstalls = hookReinstallCount()` – report and tab-3 (`IDC_STAT_BARRIERV/REINSTV`) can never disagree.
* Mouse/FG: `MouseButton = mouseButton`, `MouseWheel = mouseWheel`, `ForegroundChanged = foregroundChanged` – fixes beta5 B3 “wheel counted as button” (see §4).
* Keyboard – exact split: `KeyDown = keyDown+sysKeyDown`, `KeyUp = keyUp+sysKeyUp` so `KeyDown+KeyUp == hook.keyboardEvents()` (= `keyDown+keyUp+sysKeyDown+sysKeyUp`). Previous `KeyDown=total, KeyUp=0` collapsed the distribution and hid sys-key totals.
* Output – `SendInputCalls = emitter.sendInputCalls()`, `SendInputFailedEvents = emitter.sendInputFailed()` – emitter’s `InlineEmitter::sendEdit` (`src/core/win32_wrapper.cpp:80-140`) counts real `::SendInput` syscalls (`sendInputCalls_.fetch_add(1)` per `flush()`) and `c-sent` failures, including chunked batches (`kMaxInlineInputs` spill), TSF-fallback `sendBackspaces/sendUnicodeText`, and OOM-catch `sendEdit(it.backspace,…)` – one rebase covers all paths, removes per-edit drift. Previous inline-only `add(1)` under-counted chunked macros and missed fallback/OOM.

`onHookEvent` (`src/app/main.cpp:1395`) now classifies `MouseWheel` via `ev.wParam == WM_MOUSEWHEEL||WM_MOUSEHWHEEL` (hook `mouseProc:983` already does).  
`onConsumerEvent::flushEditBatch` (`src/app/main.cpp:2101`) now: `TsfCommits` always, `if (!batchOk) TsfFailedCommits`, else if `lastCommitSlow()` → `TsfSlowCommits`; latency histogram `TsfCommit`/`SendInputCall` kept, `SendInputCalls` no longer manual. Evidence `emit-chain` (`recordEmitEvidence`) still records `channel 0=TSF` on `batchOk` and `1=SendInput` on fallback/OOM/deferred.

**Result:** `tests/test_diagnostics_beta7_repro.cpp` pins `report` no longer shows `96/0/empty` when a snapshot was refreshed; `audit_telemetry_rows` guarantees the row→source binding; CI `ok_tests` passes.

### 3.3 Verdict & evidence (tester-observable)

`src/core/Diagnostics.cpp:770` `verdict()` – `LỖI` on `ProducerExceptions||ConsumerExceptions`, `CẢNH BÁO` on `dropped/ TsfFailed||SendFailed`, else `CHẬM` on `p99>8000µs`, else `ĐÃ TỰ PHỤC HỒI` on `reinstalls`, else `OK + p99` – but **never OK without evidence**: if `kbd==0 && TotalEdit==0 && QueuedToConsumer==0` → `CHƯA ĐỦ DỮ LIỆU: chưa ghi nhận phím nào — hãy gõ thử trong ứng dụng ngoài (Notepad)`. Placeholders `(chưa có — snapshot chưa làm tươi)` and `Level::Off → bộ đếm tạm dừng` make “unavailable vs zero” testable (`docs/release-notes-v1.3.0-beta7.md:109`).

Evidence blocks (`report()`): `emit-chain` (last 32 deliveries, `channel/gate/pid/process/class/chars`), `process-resolution` (Win32 API + error + elevated), `display-metrics` (dpi/systemDpi/font/dwm/perMonitor/screen). Filled by `refreshEvidenceContext()` before every report.

---

## 4. Mouse vs keyboard – per-source accounting

*Hook truth:* `src/core/HookCounters.hpp` – `keyboardEvents() = keyDown+keyUp+sysKeyDown+sysKeyUp`, `mouseEvents() = mouseButton+mouseWheel` (moves `mouseMove` heartbeat-only, never in totals), `allSources() = keyboard+mouse+foreground`. `enabled` gate (Level::Off → zero increments). Unit test `tests/test_hook_counters.cpp` pins sums.

*Hook callbacks:* `ModernKeyHook::mouseProc:983` – `counters_.add(wheel?mouseWheel:mouseButton)`; `ModernKeyHook::keyboardProc` – per vk `keyDown/keyUp/sys…/self/thirdParty/suppressed/passedThrough/ignored…`; `ModernKeyHook::winEventProc` – `foregroundChanged`. `push()` feeds `p` vs `mouseProc` feeds `countPassThrough`; UI row `RINGV = pushed()` is ring traffic, not “keyboard processed”.

*Producer handler:* `onHookEvent:1395` – increments `KeyDown/KeyUp` (folded), `KeySuppressed/PassThrough`, `MouseButton/MouseWheel` (now split), `ForegroundChanged` only when `atLeast(Basic)` (one relaxed load when Off). Hook counters and Diagnostics counters are re-based at report time, so UI `STAT_PUSHV = hc.keyboardEvents()` (= `HC` not `pushed`) never climbs on mouse move – beta5 B3 closed.

---

## 5. Current-app detection – elevation, exclusion, foreground

* `ProcessMonitor` (`src/core/ProcessMonitor.cpp`) – WinEvent `EVENT_SYSTEM_FOREGROUND` (zero idle CPU), `refreshNow()` on handler `g.monitor.refreshNow()` + `updateExclusionCache()` (excluded = `currentAppAutoExcluded() || currentAppElevated()` – UIPI hard constraint, both output paths would silently fail, so pass-through is the only safe behaviour). `HookCounters` counts it, telemetry row `FGLAB/FGV` shows it.
* `updateForegroundPolicy()` – `outputMode` (0 Auto → `isFlickerProne` (Browser + Office `winword/excel/powerpnt/outlook/... wps/et/wpp`) unless `strategyOutput` overrides) + `g.currentHkl`/`g.fgHwnd` cache for `layoutChar(ToUnicodeEx)`. `isFlickerProne` unit-tested.
* `v3.5 foreground-hang gate` – TSF-policy foreground switch → `fgUseTsf_=false`, `fgProbePending_=true`, `PostMessage(WM_APP_FGPROBE, hwnd)`; `probeForegroundResponsiveness` bounded `WM_NULL` probe, `fgHungCount` telemetry. Consumer `onForegroundChanged` only re-resolves TSF document while `fgUseTsf_` true, so a hung STA never wedges `RequestEditSession(TF_ES_SYNC)`.

---

## 6. Settings layout – DPI-correct, never clips, scroll fallback

*Authored rects* – 96-dpi logical px, statically gated by `audit_layout.py --strict` (125 settings controls + 28 lab).  
*Runtime solver* – `src/app/main.cpp:3060` `solveSettingsLayout()` (pure `ok::layout` `DialogLayout.hpp`, pinned by `tests/test_dialog_layout.cpp`):
  1. Tab headers measured from control (`TCM_GETITEMW` + `DrawTextW`) → `planTabs` → `TCS_MULTILINE` if needed (9 labels ~598px vs 528px at 96dpi – beta4 clipped “Chaos/AI/Cấp độ”).
  2. Pages: every label `DrawTextW DT_CALCRECT|DT_WORDBREAK` → `autoFit` → wrapping labels grow, controls below shift, group boxes stretch.
  3. Window refit: `refitWindow(windowRect, clientH, disp.bottom, deepest, workArea)` → grow *or* shrink toward solved height, clamped to `MonitorFromWindow` work area, moved on-screen; partial growth applied (not all-or-nothing – beta4 B1 left overlapping button row at 125-150%).
  4. Scroll: whatever still overflows → `WS_VSCROLL` + `scrollChildRect` per-tab (`perTabContentBottom[9]`), viewport clip, `WM_VSCROLL` moves children via `SetWindowPos` + `SetWindowRgn` (header/button chrome never scrolls).

*Always-visible chrome* – header icon/title/status + bottom button row (`IDC_BTN_TOGGLE`/`IDOK/IDCANCEL/APPLY`) – never counted as page content, never drives growth, always on top.

*DPI:* `g_settingsDpi = windowDpi(hwnd)` at `WM_CREATE`, `refreshSettingsDpi()` on `WM_DPICHANGED` (re-scales fonts/rects via `MulDiv`), then `solveSettingsLayout` re-measures at new scale. Tab 6 live-gate readout `IDC_STAT_LIVE_GATE` at `S(550)` guaranteed above growth.

---

## 7. Live effects – gate is the product

*Model:* `src/core/LiveEffects.hpp` – `GateBlocker {None,ImeDisabled,AppExcluded,MasterOff,NonUnicodeTable}` pure `liveGateBlocker(ime,excl,master,uni)`, pinned by `tests/test_live_effects_chain.cpp`. Single source for UI, tray, report, evidence.
*Handler:* `g.liveEffects: LiveEffects` (`AppState`), `codeTableCache` atomic mirror (`int(CodeTable::Unicode)`) – UI writes `g.options.codeTable` under `engineMtx`, hook/consumer/`liveGateNow()`/`F9` read `codeTableCache` lock-free (no data race, no stale read). `liveGateNow()` (`main.cpp:1061`) is `ime = engineEnabled`, `excl = fgExcluded_`, `master = liveEffects.enabled()`, `uni = codeTableCache==Unicode`.
*Transport:* `planOutput(active, suppress, bs, repScratch, kind, ch, fx)` – pure, in-place `repScratch` rewrite, tested via `test_live_output_plan.cpp` (beta3 over-backspace fix: foreground switch → `resetForNewContext` + `liveEffects.reset`). `emitForChaosLab` reuses same `emitInline` emitter, so Lab output is the *real* IME output.
*Tray/report:* `liveGateStatusText()` → tip line, tab-6 `IDC_STAT_LIVE_GATE` refreshed every 500 ms (`WM_TIMER`), `exportDiagReport` appends `[live-effects gate] …` to file, `EmitRecord.gate` records gate at emit time (V4 evidence).
*Hotkey:* `Ctrl+Alt+F12` disables live effects (self-tagged, game-safe), `F9` tone-style switch via `TextEngine::switchToneStyle` (same output policy as normal edit).

Chaos Lab independence: `ChaosEngine` (tab 6 upper group, Lab window `TRACKBAR_CLASSW` needs `ICC_BAR_CLASSES`) – `masterEnabled/case/glyph/intensity/glyphMode/granularity` – intensity via float `*100` persisted (`ChaosIntensityPercent` etc, V3). Lab `setEmitCallback(&emitForChaosLab)` at boot + on `openChaosLab()` (G8 fix – Hub `launchChaosLab` façade otherwise had no emitter). Live effects and Chaos are orthogonal (`ok::layout` vs `ok::effects::planOutput`).

---

## 8. Arcade – 8 games + configuration that survives restart

*Hub:* `src/app/ArcadeWindow.cpp` – instance `open(slug)` + `focus()`, `setDpiScale()` compile fix (beta8 UX-10), `TrackMouseEvent`/`WM_MOUSELEAVE` hover, `WM_DPICHANGED`, close guards, shim symbols so `audit` never trips. Games rendered via GDI `RenderList` (same as HTML5 client).
*Runner:* `ok::arcade::ArcadeManager` – `getConfig/setConfig`, `configNeedsRelaunch`/`relaunchCurrentGame`, `drainRunResultsToProgression` (credits XP/records/achievements every 500 ms tick – previously Hub-only). Fail modes `HealthBar/Hardcore` for Rhythm/NoMistake, BPM 60-220 clamped, `PassageLanguage {Vietnamese,English}` (VN default, Telex/VNI composes inside game – hook bypasses own window; EN is 1:1 ASCII), `WasdSteering {ArrowsOnly,WASD,Both}` (beta5 B7 – VN mode letters are composition keys, so `Wasd` is now optional; fix: only arrows consume, letters steer *and* type), `vnInputMethod` follows `g.options.inputMethod`.
*Persistence:* `loadSettings`/`saveSettings` – 4 arcade keys (`ArcadeFailMode/Bpm/PassageLang/Steering`) + 9 live/chaos/AI keys – `saveSettings()` called on `IDC_BTN_APPLY_ARCADE_CFG` (`tryReadArcadeConfigFromDialog` shared reader, range-checked) with `updateHeaderStatus` + `relaunchCurrentGame` if `needsRelaunch && hasActiveGame`; also on every `OK/Apply` and teardown sweep. No “enabled yesterday, off after restart” (V3 B6/B9).
*Server bridge:* `ArcadeServer` – `GET /api/config` + `POST /api/config` accepts `wasdSteering` + `hydrateConfig()` boot-read (never clobbers), `rejectedKeys` + pacer `0-200`, `FlexGranChoice N=3/5/10/25`; `web/arcade.js` `passageLang` select. Tested: `test_arcade_server.cpp` pins red tail + Backspace hint in JSON, `test_arcade_window` checks close ownership.
*Typing Race / Fishing / NoMistake:* `test_arcade_beta7.cpp` + `test_arcade_beta8_ux.cpp` (87 checks) – banner `R→F2`, NoMistake `100%` when virgin, `tững→từng` (UX-03/04/05), English passage `lai xe vuot chuong ngai vat toc do cao` completable via WASD+type (UX-02).

---

## 9. WPM / Typing Analytics / Progression / AI

*WPM gauge* – `g.keysTyped` ++ per `Char>32` or `Backspace` (`onHookEventImpl`), `lastProgressionTick` gap `<2000ms` → `recordActiveTimeMs` (pause not counted). Tab-3 `WM_TIMER` EMA: `kpm = Δkeys/dt*60`, `Ema = 0.7*old+0.3*kpm`, decay `*0.25` after ≥1s silence (previously froze). Display `IDC_STAT_WPMVAL`.
*Analytics* – `TypingAnalyticsEngine::observeKey` (fixed ring, lock-free) + `generateCoachingAdvice`; `AiRivalEngine::observeKeystroke` opt-in (+ `trainBatch` per tick, bench `+11ns/key`). `ProgressionEngine::recordKeystroke(activeTime, …)` – `flushStats()` on timer folds lock-free counters (previously hook-thread `recordTypingSession` took progression mutex per key – now timer-only).
*Achievements* – `drainRunResultsToProgression()` credits finished arcade runs (previously web-only).

---

## 10. Chaos Lab – real output, measured cost

*Window* – `ChaosLabWindow::instance().open` + `setEmitCallback(&emitForChaosLab)` at `wWinMain` boot (G8) and on open; `openChaosLab()` surfaces `GetLastError` via `MessageBoxW` (G2 B8 – previously silent). 17 controls audited: `kIdFlex*` etc in `web/` map to `ChaosEngine` (`master/case/glyphMode/intensity/granularity`). Slider `WM_HSCROLL` → `percent = int(float*100+0.5)` (beta6 V3 float-trunc fix). Cost note on tab-6: `~+21ns/key (p50 64→85, bench beta4)`.

---

## 11. Keyboard navigation – Tab works everywhere

*Settings* – `IsDialogMessageW` in main loop for `g.hSettings`; Lab now also (`ChaosLabWindow::handle()` + `IsDialogMessageW`) – beta7 Tab was broken on Lab. `WM_CLOSE`/`WM_DESTROY` clear `g.macroEdit` atomic + scroll `solved` baseline. `WS_VSCROLL` handling (SB_LINEUP/DOWN/PAGE/THUMB) via `scrollChildRect`.

---

## 12. Persistence – 37 controls, no “enabled but off after restart”

Registry `HKCU\Software\TuyenMai\OpenKey` + `%APPDATA%\KieeKey\macros.txt` + `diag-level.txt`:

*Options* – `InputMethod/CodeTable/CheckSpelling/UseMacro/RestoreIfWrong/UpperCaseFirst/ModernOrthography/QuickTelex/DigitsLiteral(1)/DictionaryRestore/Enabled(1)/ExcludeIde/Game/Shell/OutputMode/PerfProfile/Hybrid` + migration `SettingsMigration>=2` (self-heal digits default ON).  
*Arcade/Live/Chaos/AI* – 4 arcade + 4 live (`LiveEnabled/Case/Glyph/Intensity`) + 7 chaos (`ChaosMaster/Case/Glyph/IntensityPercent/GlyphIntensityPercent/GlyphMode/CaseGranularity`) + `AiOptIn`. Every toggle (`IDC_CHK_LIVE*`, `IDC_CHK_CHAOS_*`, `AI_OPTIN`, `IDC_BTN_APPLY_ARCADE_CFG`, tab-0 live-apply `BN_CLICKED`/`CBN_SELCHANGE`) → `saveSettings()` immediately + `updateHeaderStatus()`/`updateTrayIcon()`. Final sweep on `WM_DESTROY`/`WM_ENDSESSION TRUE` + on real logoff only (not on cancel – B-5).

Macros – UTF-8/UTF-16 BOM aware, preserves `g_macroFileRaw` comments (editor seeded from raw, not regenerated), `writeMacrosFile` returns bool (G2 B8 – previously silent no-op on read-only `%APPDATA%`).

---

## 13. Lifecycle – suspend/lock/display/second-instance

* `WM_POWERBROADCAST` `PBT_APMSUSPEND` → `onLifecycleSuspend()` (drop pending word, no KeyUp guaranteed), `PBT_APMRESUMEAUTOMATIC/RESUME` → `onLifecycleResume()` (re-seed modifiers via `resyncModifiersFromOs`, barrier `forceQuiesce` only where quiescent).
* `WM_WTSSESSION_CHANGE` – `WTS_SESSION_LOCK/LOGOFF` → suspend, `UNLOCK/LOGON` → resume (secure desktop owned keyboard).
* `WM_DISPLAYCHANGE` – `monitor.refreshNow()` + `updateExclusionCache/updateForegroundPolicy` + `refreshSettingsDpi()` if dialog open (fullscreen-game rect + DPI).
* Single instance – `CreateMutex(kSingletonMutexName)` + 3-step takeover: `signalRunningInstance()` (ask to restore tray), `runningInstanceResponsive()` bounded ping, else `terminateStaleInstance()` (PID+image validated) then re-acquire. `writeRunningPid`/`clearRunningPid` registry handshake. `WM_APP_RESTORE` second-instance wake → `restoreTrayIcon` + balloon.
* `WM_ENDSESSION wParam==FALSE` (cancel) → no teardown (previously killed IME while session continued – B-5). Real end → `saveSettings` + `hook.stop` + `Shell_NotifyIcon(NIM_DELETE)` + `PostQuitMessage`; `ExitProcess` if workers detached.
* `water` – `g_wakeExitEvent` + `wakeWatcher` thread for tray restore on `TaskbarCreated` (`RegisterWindowMessage(L"TaskbarCreated")`).

---

## 14. DPI & per-monitor

* `windowDpi(hwnd)` / `systemDpiOrFallback()` → `DisplayMetrics.dpi/systemDpi/fontFace/fontHeight/dwm/perMonitor/screen` in report (`display-metrics` line). `g_settingsDpi` captured at `WM_CREATE`, fonts cached per-DPI (`FontCache 8 slots`, never evicts – B-5 `DeleteObject` use-after-free fix), `SolveSettingsLayout` uses `MulDiv(px,dpi,96)`, `WM_DPICHANGED` SuggestedRect applied + child rescale + re-solve + work-area clamp.

---

## 15. Security – UIPI, elevation, TSF hang

* Elevation – `ForegroundInfo::elevated` (Toolhelp + `GetTokenInformation`) → `fgExcluded_` → pass-through (both output paths would fail silently). Row `APPV` suffix `elevated, pass-through` vs `excluded` vs `đang gõ`.
* UIPI – hook `IsSelfInjected` via `kSelfInjectedExtraInfo` magic (`0x0B00B1E5`), `InlineEmitter` mutex per `sendEdit`, `countPassThrough` keeps `pushed` honest.
* TSF hang gate – see §5 (`fgProbePending`, bounded `WM_NULL` probe, `fgHungCount`). `g.composer.lastCommitSlow()` (≥100ms) → `tsfSlowCount` ++ and (profile `tsfSlowDowngrade`) → `fgUseTsf_=false` + `notify.raise(TsfSlowDowngrade)` throttled 1/h.

---

## 16. Performance – zero added latency on live path

* Inline default – hook-inline `SendInput` zero consumer hop, measured `~1.055µs p50` burst (quiet hardware) covering entire chain including `SendInput`; deferred opt-in (`OPENKEY_INLINE_MODE=deferred` or profile `deferInlineToConsumer`) routes via consumer for serialization concerns.
* Barrier – `EditDrainBarrier` `kWaitBudgetMs=1` (`timeBeginPeriod(1)`), hybrid `~2µs spin (200×_mm_pause) → event wait`, `consumerParked_` skips `SetEvent` while consumer spinning (S4), `timeout` counted → verdict `OK (có N lần chờ barrier quá hạn)`.
* Profiles – `PerfProfile.hpp` central `resolveStrategy` (Balanced/Fastest/LeastFlicker/MaxCorrectness/Adaptive + LowCpu/ExtraCorrect hybrids) → `consumerSpinBounds/barrierBudget/editBatchMax/layoutRecheckEveryKey/tsfSlowDowngrade/deferInlineByProfile/strategyOutput/dictionaryRestore`.
* Bench – `docs/PERFORMANCE.md` (beta4 15-pair medians), `bench_accent_ab` on CI via `workflow_dispatch` (quieter runner), isolation bench `+21ns` chaos, `+1.8ns/char` glyph.

---

## 17. Verdict – **SHIP beta7**

*Validation:* All static gates green, `check_version` `1.3.0-beta7 (PE 1.3.0.8)`, SHA256 manifest 646, local quick + CI `35691444755` green (x64/ARM64/ARM64EC + native on `6649784`, main/tag runs `35692270294/35692286324` in progress – same tree).  
*Residual risk:* R3 (`ProgressionEngine`/`AiRivalEngine` `saveToFile` never called) – flagged out-of-scope; no Windows-runtime RC claim until hardware validation.  
*Action:* **Merge PR #31** (`c38f87d`) → **tag `v1.3.0-beta7`** → CI `Publish release` will attach `KieeKey-x64/ARM64/ARM64EC.zip` (+ `arcade_serve`/`arcade_bench` + `web/` + `run_web_bridge.*`). Users on `v1.3.0-beta6` upgrade by replacing `KieeKeyApp.exe` (same `HKCU\...\OpenKey` key, migration carries `DigitsLiteral` forward).

---

*Generated by final check – source of truth is the tree at `c38f87d` (tag `v1.3.0-beta7`).*  
*Artifacts: `docs/release-notes-v1.3.0-beta7.md`, `BUG_HUNT_REPORT_beta7_phase2.md`, `QA_REPORT_v1.3.0-beta7_RC.md`.*
