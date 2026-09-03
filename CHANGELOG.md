# Changelog

All notable changes to KieeKey are documented here. Format based on
Keep a Changelog; versioning: SemVer.

## [1.1.1] — 2026-09-03

### Removed — the global Ctrl+Shift toggle hotkey (root cause of "the IME
### suddenly turns off for no reason")

The final user report pinned the residual trigger: touching Ctrl+Shift
switched the input method OFF with nothing shown and no obvious way back.
Across 1.0.x–1.1.0 the chord state machine was hardened repeatedly (bare
chord detection, injected-event filtering, resync-dirtying), but the
fundamental conflict remains: a bare Ctrl+Shift press+release is ALSO the
Windows "between input languages" chord and the prefix of dozens of
application shortcuts, third-party software injects it (RDP clients, key
remappers, automation tools), and a missed modifier key-up (UIPI/secure
desktop, RDP, another hook's swallow) can make chords the user never typed
fire. A keyboard-level toggle for an input method is therefore inherently
phantom-toggle-prone. v1.1.1 removes the mechanism entirely:

* `src/core/CtrlShiftChord.hpp` deleted; the hook-side chord tracker and the
  whole hotkey branch in `onHookEvent` are gone. The keyboard hook can no
  longer change the on/off state AT ALL.
* `tests/extreme_toggle.cpp` (the chord fuzz harness) removed together with
  the feature it tests; the chord section in `tests/test_hotfix.cpp`
  dropped as well.
* On/off is now EXPLICIT and IN-APP only:
  * Tray menu first item states the action: "Tắt gõ tiếng Việt" (running) /
    "Bật gõ tiếng Việt" (stopped).
  * Settings dialog: an always-visible toggle button on the button row
    ("Bộ gõ: ĐANG BẬT — bấm để TẮT" / "ĐANG TẮT — bấm để BẬT"), live-refreshed
    every 500 ms, reachable from every tab.
  * Both paths share one `toggleEngineFromUi()` helper: it drops stale
    engine word state, re-seeds the hook's modifier tracker when re-enabling
    (safe — single atomic store), updates the tray icon, PERSISTS the state
    immediately and shows a confirming tray balloon ("Đã BẬT/TẮT gõ tiếng
    Việt"). No toggle can happen silently anymore.
* `WM_APP_TOGGLE` retired (the hook thread no longer notifies state
  changes); tooltips/balloons/startup balloon no longer mention any hotkey.

### Fixed — stale on/off state after exit and relaunch

The v1.1.0 persistence model wrote `Enabled` at every change point, but the
only writer besides the UI was the hotkey toggle itself — a phantom Ctrl+Shift
silently saved Enabled=0, so the next launch "showed the old configuration"
(the IME off, or a state the user never chose). With the hotkey removed,
state changes can only come from visible UI actions; on top of that:

* `saveSettings()` now also runs on the clean-exit path (`WM_DESTROY`) and
  on a real session end (`WM_ENDSESSION` with wParam TRUE) — the registry
  always mirrors the last state the user saw, never a stale value.

### Changed — version consistency v1.1.1

All release-identity strings bumped and consistent:
`kAppVersion`/`kAppTitle` (about + tray tooltip), `.rc` VERSIONINFO
(1,1,1,0 / FileVersion / ProductVersion), application manifest
assemblyIdentity 1.1.1.0, CMake `project(KieeKey VERSION 1.1.1)`, the public
`OPENKEY_KIEEKEY_VERSION_STRING` ("1.1.1"), the WinUI 3 surface (window
caption badge, status line) and every source-file release banner. Historical
inline annotations (`v1.1.0:`) and the verbatim lineage reports under
`docs/reports/` intentionally keep their original version labels.

## [1.1.0] — 2026-09-02

A broad reliability, correctness and UX release: engine edge cases, hook
modifier tracking, TSF lifetime management, shutdown hardening and a
DPI-aware settings dialog with a real macro feature.

### Consumer-layer + toggle-reliability amendments (2026-09-02, same 1.1.0 drop)

Fixes the "tone marks make my letters uppercase" / "English word replaced
or mangled after an accidental tone key" family AND the "the IME suddenly
turns off for no reason and I don't know how to open it again" family
reported by users. The core engine (TextEngine) is byte-identical
throughout — verified by re-running the full Linux test suite (`ALL
TEXTENGINE TESTS PASSED`), a ~660k-sequence brute/differential fuzz against
the vendored 2.0.5 engine (Telex + VNI, incl. backspace and mixed-case
contract sequences: no over-backspace, no committed-text drift, no
uppercase output from lowercase input), and the golden vectors.

* **Fixed — stale tracked modifiers composed the WRONG CASE into the word
  buffer (the reported "tone marks make letters uppercase" bug).** The
  hook's delta-tracked `modifierBits_` (feeding `layoutChar()` /
  `produceChar()` and therefore `TextInput::isCaps` → the engine's
  `kCapsMask`) desynchronized from the real OS state whenever Shift or
  CapsLock key-ups were missed: a UAC / secure-desktop / elevated-window
  focus period (UIPI delivers no LL-hook events), an RDP transition, or
  another application's hook swallowing modifier key-ups. While stale,
  every PASS-THROUGH letter rendered with the true (lowercase) system
  state, but the engine's word buffer carried `kCapsMask` — the next tone
  mark / đ / â transform re-emitted the letter from that buffer with the
  wrong case: typing "vo" + `j` showed "vỌ", "as" showed "Á", "chaof"
  showed "chÀO". Two fixes:
  * `ModernKeyHook::resyncModifiersFromOs()` (new) re-seeds the tracker
    from `GetAsyncKeyState`; the app calls it on every ForegroundChanged —
    the same treatment the Ctrl+Shift chord tracker received in 1.1.0 for
    the same missed-key-up class — and on the Ctrl+Shift toggle (covers
    re-enabling after a desync window while the IME was off). Hook pump
    thread only, serialized with `applyModifierDelta`.
  * `applyModifierDelta` now tracks INJECTED CapsLock/NumLock/ScrollLock
    keydowns too. An injected toggle (On-Screen Keyboard, RDP, macro
    tools) DOES flip the real system state; the previous `!ev.injected`
    guard skipped the XOR and left the tracker on the old state. KieeKey
    itself only ever injects VK_BACK and KEYEVENTF_UNICODE (wVk == 0), so
    tracking injected toggles cannot self-feed.

* **Fixed — three phantom-toggle paths fired the Ctrl+Shift hotkey without
  the user pressing it (the reported "it suddenly turns off for no reason
  and I don't know how to open it again").** All three were reproduced by
  the extreme toggle harness (`scripts/extreme_toggle_tests.cpp`, 600k
  fuzzed event segments + 9 scripted scenarios) on the pre-fix tree and
  pass on the fixed tree; the pristine 1.1.0 tree fails 3 of them:
  * *Contamination erasure hole*: the `Dirty` marker was reset by the same
    event that set it whenever the second modifier was not yet held, so
    `Shift↓ H↓ H↑ Ctrl↓ Ctrl↑ Shift↑` — typing a CAPITAL and then tapping
    Ctrl while Shift was still held — TOGGLED the IME (the everyday
    "typing normally and it turned off"), and `Ctrl↓ S↓ Shift↓ Shift↑`
    still toggled despite the earlier audit fix. The chord state machine
    now keeps contamination CUMULATIVE until both modifiers are released
    (`CtrlShiftChord.hpp`, rewritten).
  * *Injected chords*: third-party INJECTED bare Ctrl+Shift (RDP clients
    forwarding keystrokes, key remappers, automation tools) armed and
    fired the toggle. Injected modifier events are now filtered out in
    `onHookEvent` before feeding the chord (KieeKey itself injects only
    VK_BACK / KEYEVENTF_UNICODE, so a real chord can never be missed);
    injected non-modifier keydowns still cancel, so a remapped key between
    the modifiers remains "a key in between".
  * *Resync arming*: a foreground change while both modifiers were held
    re-seeded the chord as CLEAN, so releasing them toggled a chord the
    app never observed starting. `resync()` now marks an unobserved chord
    Dirty — only chords pressed AND released entirely between two resyncs
    can toggle.
* **Changed — toggle feedback (the "don't know how to open it again"
  half).** The hotkey toggle was completely silent. It now shows a tray
  balloon on every toggle ("Đã TẮT/BẬT gõ tiếng Việt — nhấn Ctrl+Shift để
  bật/tắt lại"), and the startup balloon states the ACTUAL persisted state
  (previously it claimed the IME was ready even when launching in the
  persisted OFF state) and teaches the recovery chord. The persisted
  on/off state itself is kept (OpenKey parity).

### Post-release audit amendments (2026-09-02, same 1.1.0 drop)

Full-source audit (edge cases, correctness, consistency, stability,
speed) with the complete regression battery re-run after every fix —
see [docs/reports/POST_RELEASE_AUDIT_REPORT.md](docs/reports/POST_RELEASE_AUDIT_REPORT.md).
The version is unchanged; the source drop is amended in place.

* **Fixed — macro re-expansion regression (introduced by the v1.1.0 D3
  model).** After a space-triggered ReplaceMacro, `hasHandledMacro_` was
  never re-armed (`spaceCount_` stayed 0, so the legacy "next letter
  restarts the session" trigger never fired): typing the same
  abbreviation twice produced raw letters the second time ("xl xl " →
  "xin lỗixl " instead of "xin lỗi xin lỗi ", verified against the real
  2.0.5 engine). The expansion now resets the full word state (mirroring
  the word-break tail) and the flag re-arms when the next word's first
  letter arrives; `macroExpandLen_` is dropped once new text follows the
  expansion (backspaces then delete the new text, not the committed
  expansion).
* **Fixed — `handleModernMark` rule 3.2.** v1.1.0 activated 2.0.5's dead
  `ia/ya/ua` branch; the activation forced the tone mark onto the FIRST
  vowel of every `U+A` group, diverging from both the real 2.0.5 engine
  and the clean-room oracle exactly where the dead code never fired
  ("ua" + end consonant — the quán family — and 3-vowel "uai…" groups).
  The branch is dead again, exactly as upstream shipped it; rules 4/7
  already produce every documented placement.
* **Fixed — oracle (test-side spec) mirrored** to the engine's corrected
  semantics (break-key macro state reset + fresh-letter re-arm); the
  oracle previously kept 2.0.5's `hasHandledMacro` leak, which silently
  gated the spelling-restore path for the word following a comma/enter
  macro expansion.
* **Fixed — `ProcessMonitor::start()` failure path** left the pump thread
  joinable with `running_` cleared, so `stop()` never joined and
  `~ProcessMonitor()` called `std::terminate()` (abort-at-exit whenever
  `SetWinEventHook` fails). Bounded join/detach added.
* **Fixed — `TsfComposer::textBeforeCaret()`** dropped the
  `TF_ES_ASYNCDONTCARE` retry: a deferred `DoEditSession` wrote through a
  dangling stack `HRESULT*` and a possibly-released `ITfContext*`
  (stack-use-after-return + COM use-after-free); the retry could never
  produce a usable result. Sync request only, graceful "unavailable".
* **Fixed — COM init balance in `TsfComposer`**: `ownsComInit_` tracks
  whether `CoInitializeEx` actually took a reference (S_OK/S_FALSE do;
  `RPC_E_CHANGED_MODE` does not) — failure paths no longer unbalance
  another owner's reference and `detach()` now balances its own.
* **Fixed — singleton takeover startup race**: a second launch inside the
  first instance's window (mutex held, main window not yet created) was
  misread as a zombie and terminated; the probe now retries the window
  find for 3 s before concluding.
* **Fixed — `WM_ENDSESSION` honors `wParam`** (a *canceled* end-session no
  longer stops the hook/monitor mid-session).
* **Fixed — macros.txt file write moved out of `engineMtx`** (disk I/O
  under the hook-thread lock could stall typing past the LL-hook timeout).
* **Fixed — `ModernKeyHook`**: start() failure now rolls back the full
  lifecycle; restart after a stuck-detach is refused (two consumers on
  the SPSC ring corrupt it); the consumer µs clock no longer overflows
  at ~9–21 days uptime; `reinstallHooksOnPump()` re-seeds the modifier
  tracker (a release missed during a dead hook no longer sticks "held").
* **Fixed — `CtrlShiftChord`** dirties on a non-modifier key-down while
  EITHER modifier is held (Ctrl↓, key↓, Shift↓, Shift↑ no longer toggles).
* **Fixed — TSF `applyDelta`** returns `E_FAIL` for a selection-less
  context (was `S_OK`: keystrokes silently vanished with no fallback).
* **Fixed — `Win32RAII`**: dedicated `FindHandle` with the correct
  `INVALID_HANDLE_VALUE` invalid value; move-assignments use
  `std::addressof` (the out-param `operator&` overload hijacked `&other`).
* **Fixed — app hygiene**: tray menu posts the KB135880 `WM_NULL`;
  foreground-change tooltip refresh no longer persists 13 registry values
  per app switch (new `WM_APP_UPDATE_TIP` handler); the settings font is
  cached per DPI; the macro editor is seeded from the raw macros.txt
  content (user comments survive Open→OK); `ifstream/ofstream` take
  `.c_str()` so the app compiles with the project's own MinGW toolchain
  path, not only MSVC.
* **Verified**: full regression battery + differential harness
  (`run_mega_bench.sh --fast`, 3.29 M events) ends at
  **VERDICT: PASS (0 text divergences)**; three-engine bench
  216.6 ns/key mean (p99 546 ns); Windows exe built fully static
  (MinGW-w64 x64).

### Fixed — engine correctness (src/core/TextEngine)
* **Out-of-bounds read at word start** — `checkForStandaloneChar` indexed
  `typingWord_[index_ - 1]` before the `index_ == 0` guards, i.e.
  `typingWord_[SIZE_MAX]` on an empty word (inherited from the legacy signed
  `-1` port). The reverse-to-ư path still behaves identically for every
  non-empty word.
* **Dead (impossible) conditions in `handleModernMark`** — carried verbatim
  from OpenKey 2.0.5 (`Engine.cpp:671-689`): rule 3.1 compared `chr(vS+2)`
  against two different letters for the CH/NH/NG end clusters and rule 3.2
  compared `chr(vS)` against two letters for the ia/ya/ua groups. Both were
  dead code, so modern orthography placed the mark wrongly for iêch/iênh/iêng
  endings. The conditions now compare the intended positions
  (`chr(vS+2)=='C' && chr(vS+3)=='H'`, `chr(vS)=='I' && chr(vS+1)=='A'`, …);
  the rule-4 G/Q refinements still run afterwards exactly as before.
* **Context-resync desync (click / caret-edit)** — `resumeFromText` replayed
  the visible raw word through the FULL state machine, storing Telex
  transform masks in the buffer (`as`, `dd`, `aw` …). The next word break
  then "restored" composed text that was never displayed — duplicated/ghost
  letters. Resync now replays in a dedicated raw mode (`rawReplay_`) that
  mirrors the visible letters verbatim (buffer == screen).
* **Stale VNI vowel index** — the VNI branch scans the buffer for the last
  O/A/E to map digit 6/7/8; when nothing matched, the PREVIOUS word's index
  stayed in `vowelEnd_` and digit 6 composed onto a phantom vowel. The scan
  is now validity-tracked (`vniVowelEndValid_`) and the transform is skipped
  when there is no vowel to attach to (the digit passes through raw).
* **Macro/backspace bookkeeping desync** — after a `ReplaceMacro` the engine
  incremented `spaceCount_` although the space key is consumed by the
  expansion (D3 contract: no space is on screen). The first backspace after
  any macro expansion therefore ran the wrong restore path and composed onto
  phantom raw keys. New visibility model: `spaceCount_ = 0`, pending
  bracket/space bookkeeping cleared, and the expansion's UTF-16 length is
  tracked in `macroExpandLen_` so backspaceBranch walks it down like
  ordinary text (previous-word state resumes afterwards, as with normal
  words). The word-break ReplaceMacro tail now resets the full word state
  (`stateIndex_`/`tempDisableKey_`/`longWordHelper_`), not only `index_`.

### Fixed — hook / input pipeline (src/core, src/app)
* **CapsLock/NumLock/ScrollLock tracked live** — the toggle bits were seeded
  once at `start()` and never updated, so toggling CapsLock after launching
  KieeKey composed the wrong case for every keystroke (and the layout
  resolver read stale toggle state). Toggle keys now XOR their bit on each
  non-injected KeyDown; the seed uses `GetAsyncKeyState` (the documented
  async table for non-UI threads).
* **Ctrl+Shift chord self-toggle** — the chord tracker was fed purely from
  the event stream, so a missed modifier key-up (LL-hook timeout removal,
  UIPI transition, RDP switch) left it permanently "held" and the next bare
  Ctrl tap silently switched the IME off mid-work. `CtrlShiftChord::resync()`
  re-seeds from the OS key state on every foreground change.
* **Mouse wheel now breaks the word** — `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL`
  previously fell through the mouse hook, so scrolling moved the caret
  without a word break or re-sync and the next keystroke composed onto the
  pre-scroll word.
* **AltGr composes normally** — on AltGr layouts the LL stream reports
  Ctrl+Alt for plain characters; treating that as a ctrl-combo word-broke
  every AltGr character. Ctrl+Alt now composes (UniKey parity); plain Ctrl
  or plain Alt still bypass composition.
* **Dead-key layouts flush correctly** — the dead-key path repeated the same
  key, leaving the dead state armed on the hook thread (the NEXT letter was
  then composed into an accented character by the layout resolver). The
  pending dead state is now flushed with a space (the documented clear
  sequence) and the spacing dead character is returned so '^' alone still
  types '^'.
* **Numpad digits mapped** — `VK_NUMPAD0..9` previously fell through the US
  fallback map; numpad entry now participates in VNI digit marks and macro
  capture.

### Fixed — TSF composer (src/tsf)
* **Per-keystroke COM leak** — edit-session objects were created with
  refcount 1 and never released after `RequestEditSession` returned, leaking
  one session object (+ its copied payload) per committed edit — fastest in
  browsers/Office, the Auto-TSF set. The caller's initial reference is now
  released after the synchronous request (Microsoft sample semantics) on all
  three session paths.
* **Dangling-stack read-back session** — `textBeforeCaret` requested the
  session WITHOUT `TF_ES_SYNC` while pointing it directly at the caller's
  stack `std::wstring`: a deferred `DoEditSession` wrote into a returned
  frame (stack-use-after-return) and even the benign case returned an empty
  word. The session now owns its result buffer, requests
  `TF_ES_READ | TF_ES_SYNC` (with one `TF_ES_ASYNCDONTCARE` fallback that
  degrades gracefully when the document lock is busy), and the caller copies
  the result out afterwards.
* **Slow-commit watchdog** — every synchronous commit is now timed; ≥ 100 ms
  (normal is single-digit µs) marks the foreground as starving and the app
  downgrades THAT foreground to inline SendInput until the next app switch —
  the same "hung app wedges the consumer" family the v3.5 probe guards at
  switch time, now also covered after the switch.

### Fixed — stability / shutdown
* **Static-destruction race over detached workers** — when the bounded
  shutdown had to detach a wedged hook/monitor thread, the app could still
  run static destruction over it (the detached worker touches the queue and
  handles when its blocked COM call finally returns). Both pumps now publish
  `stuckThreadsDetached()`; the app exits via `ExitProcess` on that path
  (kills every thread atomically before any teardown code runs).
* **Ordering-barrier deadline uses QPC** — the 1 ms drain-barrier budget was
  previously measured with `GetTickCount()`, which only advances at the
  clock interrupt (15.6 ms default): without a successful
  `timeBeginPeriod(1)` the LL-callback stall could silently inflate to one
  full tick. The deadline is now QPC-backed (monotonic, high-resolution
  regardless of timer policy).
* `ProcessMonitor::enableCrashRecovery` now honors the caller's
  `RegisterApplicationRestart` flags verbatim (the old code silently ORed
  `RESTART_NO_CRASH | RESTART_NO_HANG` into every call, contradicting the
  documented contract).
* The `KIEEKEY_PROFILE=1` build no longer fails with C2065
  (`t_lastBarrierNs` used before its declaration).

### Added — features / UX
* **Real macro feature ("Gõ tắt")** — the 1.0.x settings checkbox was a
  silent no-op (no resolver was ever installed). v1.1.0 ships a macro table:
  `%APPDATA%\KieeKey\macros.txt` (`abbr=expansion` per line, `#` comments,
  UTF-8/UTF-16LE with BOM detection; a commented template is written on
  first run), wired into the engine `MacroResolver` contract — type the
  abbreviation, press Space, the expansion replaces it and consumes the
  space (D3). Matching folds case; the table is parsed and swapped under the
  engine lock (race-free with the hook thread).
* **Macro editor tab** — the settings dialog gained a fourth tab ("Gõ tắt")
  with a multiline editor synced to `macros.txt` (loaded on open, saved on
  OK/Apply).
* **DPI-aware settings dialog** — every coordinate is expressed in 96-dpi
  logical pixels and scaled to the dialog's per-monitor DPI at creation (the
  manifest already opted into PerMonitorV2); `WM_DPICHANGED` follows the
  monitor and the frame is sized via `AdjustWindowRect`. Fixes the
  **"Luôn SendInput" radio clipped outside the window** (placed at x=390
  with width 200 in a 480 px window — half the label was cut off at every
  DPI) by moving the three output radios onto two rows, and the font is now
  DPI-scaled.
* **Richer diagnostics tab** — new rows for ordering-barrier timeouts, hook
  self-healing reinstalls, slow TSF commits (the watchdog) and the current
  foreground application with its live auto-exclusion state; the latency
  PEAK now resets when the dialog opens (previously a process-lifetime
  outlier dominated the display) and the WPM gauge decays to zero after
  ~2 s of silence (previously it froze on the last rate forever).
* **Persisted Vietnamese on/off state** — the Ctrl+Shift / tray toggle now
  survives restarts (`Enabled` registry value; OpenKey parity — 1.0.x
  always relaunched enabled).
* **Tray tooltip explains auto-exclusion** — when the engine is paused for
  an IDE/game/shell window, the tooltip names the app ("tạm tắt trong
  …") instead of silently doing nothing (the classic "KieeKey suddenly
  stopped" report).
* `ESC`/`Enter` handling in the settings dialog now relies solely on the
  `IsDialogMessage` routing (the duplicate `WM_KEYDOWN` path raced the
  code-table combo dropdown across common-control versions).

### Changed
* Version unified to **1.1.0** everywhere: CMake `project()`, `.rc`
  VERSIONINFO (`FILEVERSION`/`PRODUCTVERSION 1,1,0,0`, strings `1.1.0.0`),
  manifest (`1.1.0.0`), first-run balloon + window titles, singleton mutex
  (`KieeKey_1.1.0_Singleton`), wake event, `kieekey_core.hpp` version
  macros (previously still the pre-release lineage `3.3.1`), file banners,
  benchmark banner strings, README. Historical documents under
  `docs/reports/` and the 1.0.x changelog sections are intentionally left
  verbatim (lineage record).
* New v1.1.0 regression tests (engine suite): OOB word-start standalone,
  raw resync replay, macro backspace model, VNI no-vowel digit pass-through.

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
