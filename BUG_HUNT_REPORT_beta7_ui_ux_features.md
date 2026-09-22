# Bug Hunt Report — KieeKey v1.3.0-beta7
## Deep product-quality audit: UI correctness, UX, features, state sync

**Audit date:** 2026-09-21
**Baseline commit:** `9468522` (`docs: add beta7 phase2 bug hunt report`)
**Branch:** `arena/01a0c46a-kieekey`
**Scope:** UI correctness/layout, state sync, interaction, feature completeness,
UX consistency, error handling, keyboard navigation, window lifecycle, settings
persistence, Arcade UI/games, desktop↔web bridge, cross-feature interactions,
regressions from beta7 fixes.

---

## 0. Method, and why it found what previous audits did not

The previous report (`BUG_HUNT_REPORT_beta7_phase2.md`) closed B7-01…B7-09 and
declared regression coverage for each. That coverage was **not real** (see
UX-09), so no prior PASS marker was trusted here.

Instead of reading code and reasoning about it, this audit **drove the product**:

* ten scripted end-to-end player journeys across all eight games;
* the **actual DOM event sequence a browser emits** for `<input type=range>`
  and `<select>` (`input`×N then `change`), rather than the single synthetic
  POST the unit tests issue;
* typing each game's **own displayed passage**, character by character, and
  asserting the passage advances;
* reading the HUD back and checking that **every key the UI advertises really
  does what the text claims**;
* 500× launch/stop and launch-without-stop accumulation cycles, 20 000-key
  repeat floods, 1 001 pause toggles.

That last class of check is what exposed the two most serious defects. Both
were invisible to the existing suite because the suite exercised a shape of
input no real front-end ever produces.

**Four of the bugs below were being actively asserted as correct by tests**
(UX-01 in `test_arcade_server.cpp`, UX-02 in `test_arcade_recovery.cpp`,
UX-06 in `web_render_test.js`, plus the never-run `test_arcade_beta7.cpp`).
The suite was green *because* it encoded the bugs.

---

## 1. Confirmed bugs

### UX-01 — `applyNow` is dead from the real web UI: the BPM slider and language selector never affect the running game
| | |
|---|---|
| **Severity** | **HIGH** |
| **Area** | Desktop↔web bridge / Arcade config (`src/core/ArcadeServer.cpp`, `src/core/Arcade.cpp`) |
| **Status** | **FIXED** |

**Symptom.** In the web hub, dragging the Rhythm BPM slider or switching the
passage language updates the on-screen control, reports success, and changes
nothing. The running game keeps the old tempo/language. No toast appears, so
the player gets no hint that the change was dropped.

**Reproduction.**
1. Start the web hub, launch Rhythm (starts at 112 BPM).
2. Drag the BPM slider to 180 and release.
3. Observed: panel reads 180; the chart still plays 112. Response:
   `restartApplied:false, restartRequired:false`.
4. Same for the passage-language `<select>`: config says English, the run keeps
   serving Vietnamese.

**Expected.** Releasing the slider rebuilds the run at the chosen tempo (that is
precisely what `applyNow` exists for) and toasts "đã áp dụng".

**Actual.** Nothing changes and the response claims there is nothing to do.

**Root cause.** `web/arcade.js` binds `pushConfig()` to `input` and
`pushConfig({applyNow:true})` to `change`. A browser fires `input` for every
intermediate value, then `change` with the **same final value**. The route
computed `configNeedsRelaunch(config)` — incoming-vs-**stored** — *before*
storing. The earlier `input` had already stored 180, so the deciding `change`
compared 180 against 180, concluded "no difference", and skipped the relaunch.
The feature could only ever work for a client that sends exactly one POST —
which is what the test did, and no browser does.

**Fix.** Staleness is now a property of the **run**, not of a config diff.
`ArcadeManager` records `m_runConfig` (the configuration the current run was
built from) at launch, and the new `runNeedsRelaunch()` compares the live
config against it. `/api/config` stores first, then asks whether the *run* is
stale. This survives any number of intermediate stores and any client event
ordering. A single `chartKnobsDiffer()` helper now backs both predicates so
they cannot drift apart.

**Regression coverage.** `tests/test_arcade_beta8_ux.cpp`
`testWebSliderEventSequenceApplies()` replays `input`×4 → `change(applyNow)`
for the slider and `input` → `change(applyNow)` for the select, and asserts the
**running game's** BPM/language actually changed — plus the converse (an
already-applied value must not claim a fake restart, and live-only knobs must
not ask for one). `test_arcade_server.cpp` was corrected: its old assertion
demanded `restartApplied:false` for exactly the case that must apply.

---

### UX-02 — WASD Race is uncompletable in English: typing its own passage does nothing
| | |
|---|---|
| **Severity** | **HIGH** |
| **Area** | Gameplay (`src/core/Arcade.cpp`, `WasdRaceGame::handleKey`) |
| **Status** | **FIXED** |

**Symptom.** In English mode the player types the passage exactly as shown and
the text never advances past its second character. Fuel cannot be replenished,
so the run always ends in "HẾT NHIÊN LIỆU" regardless of skill.

**Reproduction.**
1. Set passage language = English, launch WASD Race.
2. Passage: `lai xe vuot chuong ngai vat toc do cao`.
3. Type it perfectly. Progress stalls at index 1, on the `'a'` of "lai".
4. Final state: `textIndex = 1/38`. Identical for **all three** `wasdSteering`
   settings — choosing "Arrows" does not give the letters back.

**Expected.** Typing the displayed passage advances the passage. The steering
preference is honoured.

**Actual.** `a`, `s`, `d`, `w` are consumed as steering and never reach the
typing path. The English passage contains five of them, so the game cannot be
completed and the `wasdSteering` setting is ignored outright in English.

**Root cause.** `const bool letterSteer = !m_vnMode;` followed by
`if (steerConsumes) return InputResult::Consumed;`. beta5 fixed exactly this
class of bug for Vietnamese — where swallowing `w` would break Telex — and
introduced a non-consuming fall-through for it, but left English consuming the
letters outright. A steering letter is also a text letter; it must never be
swallowed in either language.

**Fix.** Only the **arrow keys** are steering-only (they produce no character
and can never be passage text). Letters steer *and* fall through to the typing
path, in both languages, gated on `m_steering` so the user's choice is
respected in English too. This generalises the existing beta5 `vnWasd`
fall-through rather than adding a parallel mechanism.

**Regression coverage.** `testWasdRaceEnglishIsCompletable()` types the entire
displayed passage in **both languages × all three steering modes** and asserts
zero stalls, plus explicit guards that arrows still steer, that `d` still
steers in English, and that the beta5 Vietnamese contract (Arrows ⇒ compose
only; Wasd ⇒ steer *and* compose) is unchanged.
`test_arcade_recovery.cpp`'s "EN WasdRace types ASCII 1:1" assertion expected
`index == 1` after typing `"la"` — it now expects `2`, i.e. actually 1:1.

---

### UX-10 — `src/app/ArcadeWindow.cpp` does not compile; the hub's UI test suite could not build at all
| | |
|---|---|
| **Severity** | **HIGH** |
| **Area** | Build integrity / Arcade hub window |
| **Status** | **FIXED** |

**Symptom.** `src/app/ArcadeWindow.cpp` — a file compiled into the shipping
Windows executable — contains a hard compile error at line 777. The one
portable harness that would have caught it (`ok_arcade_window_tests`) could not
be built either, because the Win32 test shim was missing three symbols the same
file needs.

**Reproduction.** Build `test_arcade_window` at baseline `9468522`:
```
ArcadeWindow.cpp:721: error: 'TRACKMOUSEEVENT' was not declared in this scope
ArcadeWindow.cpp:723: error: 'TME_LEAVE' was not declared in this scope
ArcadeWindow.cpp:725: error: '::TrackMouseEvent' has not been declared
ArcadeWindow.cpp:728: error: 'WM_MOUSELEAVE' was not declared in this scope
ArcadeWindow.cpp:777: error: 'ok::app::ArcadeWindow::Impl* ok::app::ArcadeWindow::m_impl'
                             is private within this context
```

**Root cause.** Two independent defects that masked each other:
1. The `WM_DPICHANGED` handler writes `self->m_impl->dpiScale` from the **free**
   window procedure `arcadeWndProc`, which is not a member and not a friend.
   This is a plain access violation of the class interface — rejected by every
   conforming compiler, MSVC included, so the shipped Windows build is broken
   on this path too. It is **not** an artefact of the Linux shim.
2. The hover-highlight mouse-leave tracking needs `TRACKMOUSEEVENT`, `TME_LEAVE`,
   `WM_MOUSELEAVE` and `TrackMouseEvent`, none of which existed in
   `tests/win32_gdi_shim.hpp`. So the file never compiled off-Windows, and
   defect (1) could reach the tracked tree unnoticed.

**Fix.** Added `ArcadeWindow::setDpiScale()` (mirroring the existing
`dpiScale()` getter) and used it from the window procedure; added the four
missing shim symbols plus a no-op `TrackMouseEvent` stub.

**Regression coverage.** `ok_arcade_window_tests` now **builds and passes**
(7 suites) and is exercised by `tests/run_all_tests.sh`, so this file is
compiled on every run.

> **Note.** I cannot verify the *runtime* DPI behaviour off Windows — see §3.
> What is verified is that the translation unit compiles and its logic is
> reachable; the compile error itself is compiler-independent and certain.

---

### UX-06 — Opening the web hub silently resets every desktop arcade setting
| | |
|---|---|
| **Severity** | **HIGH** |
| **Area** | Desktop↔web state sync (`web/arcade.js`, `src/core/ArcadeServer.cpp`) |
| **Status** | **FIXED** |

**Symptom.** The user configures the arcade in the desktop settings dialog
(200 BPM, English, HealthBar, pacer 140, steering Both). They then open the web
hub — merely *viewing* it, touching nothing. Every one of those settings is
silently reset to the HTML defaults (112 BPM, Vietnamese, Hardcore, pacer 60,
Arrows).

**Reproduction.** Set the five values above via `ArcadeManager::setConfig`;
issue the boot POST `arcade.js` sent at load; read the config back — all five
are gone.

**Expected.** Opening a read-only view does not mutate state. The page should
display what the engine holds.

**Root cause.** `arcade.js` booted with `pushConfig()`, which POSTs the current
**HTML control values** — static markup defaults, since nothing had populated
them yet — before the page knew anything about the server. There was no
`GET /api/config` route to read the truth from (`GET` returned 405), so the page
had no way to hydrate and could only clobber.

**Fix.** Added `GET /api/config`. Boot now calls `hydrateConfig()`, which reads
the live configuration and mirrors it onto the controls; it never writes. Both
the GET and the POST echo share one `configJson()` serializer, so the shape the
page hydrates from is exactly the shape the POST returns.

**Regression coverage.** `testWebBootDoesNotClobberDesktopConfig()` sets all
five values, exercises the read path, asserts every value survives, and asserts
the boot block no longer calls `pushConfig()`. `web_render_test.js` had an
assertion *requiring* the clobbering boot push; it now requires the opposite.

---

### UX-07 — `/api/config` reports success for values it silently discarded
| | |
|---|---|
| **Severity** | **MEDIUM** |
| **Area** | Web bridge error handling (`src/core/ArcadeServer.cpp`) |
| **Status** | **FIXED** |

**Symptom.** The route answers a flat `ok:true` whether it applied a value,
clamped it away, or did not recognise the key at all. A client cannot tell a
successful change from a discarded one, so an invalid value looks accepted
until the control snaps back on the next refresh.

**Reproduction.**
* `{"rhythmBpm":9999}` → `ok:true`, BPM unchanged, no error field.
* `{"fishingAutomation":2}`, `{"vnInputMethod":1}`, `{"wasdObstacleSpacingSec":5}`
  → `ok:true`, not applied, not even echoed.
* `{"typingRacePacerWpm":250}` → `ok:true` **and accepted**, beyond the 0–200
  range the slider and the desktop spin control offer — inconsistent with the
  sibling `rhythmBpm`, which was already range-checked.

**Fix.** The response now carries `rejectedKeys`, naming every key that was
present but could not be honoured (out of range, or unsupported by this route).
`typingRacePacerWpm` is validated against the documented 0–200 range, matching
its UI. The web client surfaces the list as a toast.

**Regression coverage.** `testConfigRouteReportsRejectedKeys()` covers the
out-of-range, unsupported-key and happy paths, and asserts an accepted change
reports an empty rejection list.

---

### UX-03 — WASD Race's game-over banner tells the player to press a key that does nothing
| | |
|---|---|
| **Severity** | **MEDIUM** |
| **Area** | Arcade UI text vs. behaviour |
| **Status** | **FIXED** |

**Symptom.** On fuel exhaustion the banner reads
`HẾT NHIÊN LIỆU — nhấn R để chơi lại`. Pressing **R** does nothing: the run
stays dead and the player is left with a game that appears frozen.

**Root cause.** Snake and Tetris call `isRestartKey(ev, allowLetterAlias=true)`
and genuinely accept R. WASD Race is a **typing** game and deliberately omits
the letter alias so `r` remains available as passage text — but it copied the
banner wording anyway. Only F2 restarts it.

**Fix.** The banner now names F2. Snake and Tetris keep their wording, which is
accurate for them.

**Regression coverage.** `testHudTellsTheTruth()` drives the race to game over,
reads the banner, and **presses whichever key the banner advertises**, asserting
the run actually restarts — a contract that holds for any future wording. The
same check is applied to Snake and Tetris.

---

### UX-04 — A freshly launched No-Mistake run displays 0 % accuracy
| | |
|---|---|
| **Severity** | **LOW** |
| **Area** | Arcade HUD |
| **Status** | **FIXED** |

**Symptom.** No-Mistake — a mode whose entire premise is a clean sheet — greets
the player with a red **0 %** accuracy gauge before a single key is pressed.

**Root cause.** `100 * currentIndex / max(1, currentIndex + mistakes)` evaluates
to `0/1 = 0` when nothing has been typed.

**Fix.** An untouched run reports 100 %; the division starts once there is
something to divide.

**Regression coverage.** `testHudTellsTheTruth()` asserts 100 % on a fresh run
in both languages **and** that a genuine mistake still drops it below 100 %, so
the fix cannot degenerate into a constant.

---

### UX-05 — Misspelt Vietnamese on the permanent No-Mistake HUD
| | |
|---|---|
| **Severity** | **LOW** |
| **Area** | Localisation / UI text |
| **Status** | **FIXED** |

**Symptom.** The hint reads `Gõ đúng tững ký tự`. **"tững" is not a Vietnamese
word** — it should be "từng". This is permanent HUD text in a Vietnamese typing
trainer, where correct diacritics are the product's core promise.

**Fix.** Corrected to "từng".

**Regression coverage.** `testHudTellsTheTruth()` sweeps the hint, status and
banner of **all eight games** for the typo.

---

### UX-08 — Chaos Lab's "characters per key" setting is hardwired to 3
| | |
|---|---|
| **Severity** | **MEDIUM** |
| **Area** | Chaos Lab UI (`src/app/ChaosLabWindow.cpp`) |
| **Status** | **FIXED** |

**Symptom.** The combo offered `{1 ký tự, 1 từ, 3 ký tự (N=3), Tự chảy}`. The
label advertises a parameter **N** that the desktop UI provides no way to
change — every selection passed a literal `3`.

**Root cause.** `game->setGranularity(static_cast<FlexGranularity>(...), 3)` —
the `3` is a literal for all four entries. `FlexingGame` supports `nChars` in
1…64 and the **web** lab has always sent a real value, so the desktop was the
only surface where the parameter was unreachable.

**Fix.** The combo now offers concrete values (1 / 1 word / 3 / 5 / 10 / 25 /
auto-stream), driven by a single `flexGranChoices()` table shared by the control
creation and the apply path so labels and applied `N` cannot drift.

**Regression coverage.** `testFlexingGranularityIsConfigurable()` asserts the
engine contract (one key emits exactly N characters for N ∈ {1,3,5,10,25}) and
that the hardwired literal is gone.

---

### UX-09 — `tests/test_arcade_beta7.cpp` shipped unwired, and did not pass when run
| | |
|---|---|
| **Severity** | **HIGH** (process/integrity) |
| **Area** | Test infrastructure |
| **Status** | **FIXED** |

**Symptom.** `tests/test_arcade_beta7.cpp` — the file the previous report cited
as regression coverage for B7-01…B7-09 — appeared in **no** CMake target and
**no** runner script. It had never been compiled or executed. Every regression
claim resting on it was unsubstantiated.

Worse: when finally compiled and run, **it failed**. It asserted
`restartRequired:true` for a config change with *no game running*, which can
never be true (there is nothing to relaunch).

**Fix.** Registered in both `tests/run_all_tests.sh` and `CMakeLists.txt`
(`add_executable` + `add_test` + the force-asserts list, with
`WORKING_DIRECTORY` set since it reads `web/arcade.js`). Its incorrect
expectation was corrected to assert the honest contract, and it now passes.

> This is the reason no prior PASS marker was trusted in this audit. A test that
> nobody runs is not coverage.

---

## 2. False alarm corrected (not a product bug)

### Settings-wiring auditor reported two phantom gaps
`scripts/audit_settings_wiring.py` failed at baseline with
`IDC_CMB_FAILMODE: read layer missing` and `IDC_CMB_PASSAGE_LANG: read layer
missing`. **Both controls are in fact fully wired** — `tryReadArcadeConfigFromDialog()`
reads them at `main.cpp:3752` and `:3776`.

The script scans a fixed ±300-character window around each control ID for a
read verb. These two resolve their handle once (`const HWND langCtl =
::GetDlgItem(...)`) and query the **local** further down than the window
reaches. A pure heuristic miss.

Rather than suppress it with an exemption entry (which would blind the gate to
a *real* future gap on those controls), the auditor now follows the
hoisted-handle pattern and credits the read only when the local is genuinely
queried with a read verb. **Verified with a negative control**: deleting the
real `CB_GETCURSEL` call makes the auditor fail again, as it should.
`src/app/main.cpp` was not modified.

---

## 3. UNVERIFIED — requires physical Windows validation

None of the following can be confirmed on this Linux host. They are **not**
claimed as passing; compile and stub-test success does not validate them.

| # | Behaviour | Why it cannot be verified here |
|---|---|---|
| W1 | **UX-10 runtime DPI rescale.** The compile fix is certain; that `WM_DPICHANGED` visually rescales the hub correctly on a real multi-DPI setup is not. | Needs real `WM_DPICHANGED` from a physical monitor switch. |
| W2 | Hover highlight actually clears on mouse-leave (`TrackMouseEvent` is a no-op stub here). | Needs a real cursor and USER32. |
| W3 | Settings persistence across restart (`ArcadeFailMode`, `ArcadeRhythmBpm`, `ArcadePassageLang`, `ArcadeSteering`). Save/load code paths read correct, but registry I/O is stubbed. | Needs a real `HKCU` and a real process restart. |
| W4 | Desktop Apply button end-to-end (message boxes, header refresh, relaunch toast). Logic verified by reading; UI not driven. | Needs the real dialog. |
| W5 | Flexing injection into an external application (`typeIntoFocusApp`, `SendInput`). | Needs a real foreground window and input queue. |
| W6 | Tray icon, global hotkeys, window z-order/focus, Chaos Lab ↔ hub ownership handshake under real window destruction. | Needs a real shell and message pump. |
| W7 | The corrected Vietnamese HUD strings render with correct diacritics in the real GDI font stack. | Needs Windows text rendering. |

---

## 4. Verified-good (probed, no defect found — do not re-investigate)

* Pause/restart across all eight games; Snake frozen while paused; 1 001 pause
  toggles leave consistent state.
* TypingRace backspace at index 0; keys after finish are inert; F2 resets score
  but preserves `highScore`.
* Double `stopGame()`; `restartGame()`/`relaunchCurrentGame()` with no game.
* 500× launch→play→stop and 500× launch-without-stop: no leak, result queue
  stays bounded (cap 64 respected).
* 20 000-key repeat floods into every game: no crash, no score corruption.
* Rhythm BPM→note-spacing maths exact at 60/112/220; HealthBar vs Hardcore
  fail modes behave differently and correctly.
* Fishing catch/escape/tension loop; NoMistake mistake→penalty→recovery.
* Hub `WndProc` key routing: exactly one `InputEvent` per press (WM_CHAR
  correctly swallowed), Ctrl/Alt/Win chords pass through to Windows.
* Desktop Apply path (`IDC_BTN_APPLY_ARCADE_CFG`, `settingsFromControls()`)
  computes `configNeedsRelaunch` **before** `setConfig` — correct, and
  unaffected by UX-01. **Deliberately left alone.**
* **T11 (progression double-credit) — investigated and dismissed.** Measured
  total credits for a single run across stop-only, drain-then-stop, and
  game-over paths: always ≤ 1. The suspicious-looking re-check in `stopGame()`
  is guarded by `score > 0` and `!alreadyReported`. **Not a bug.**

---

## 5. Remaining open risks (not fixed — reported honestly)

| # | Risk | Severity | Why not fixed |
|---|---|---|---|
| R1 | `restartGame()` drops `m_gameMtx` before taking `m_mutex` to clear `m_resultCollected`; a concurrent `update()` could theoretically drop a result. | LOW | Could not be reproduced under a concurrency hammer. Fixing it means touching the locking discipline — out of proportion to an unreproduced risk, and the user asked for no speculative work. |
| R2 | `m_active` and `m_game` can briefly disagree after a `handleKey`-driven exit; `hasActiveGame()` reads only the atomic, so `getFrame()` may render a finished game as live for one tick. | LOW | Not observable in any journey; a one-frame cosmetic window. |
| R3 | `ProgressionEngine`/`AiRivalEngine` expose `saveToFile`/`loadFromFile`/`serializeProfile` that **nothing in the app calls** — XP, levels, achievements and the learned AI profile appear to live only in memory and vanish on exit. | MEDIUM | Strongly suspected, **not confirmed**: the persistence entry point may legitimately be intended for a host that this build does not include, and confirming user-visible loss needs a real Windows run (W3). Flagged rather than "fixed" to avoid inventing a storage location. |
| R4 | `/api/config` still cannot set `rhythmNoteCount`, `rhythmApproachSec`, `noMistakeStartReserve`, `wasdStartFuel` even though `restartRequiredKeys` advertises them. | LOW | Now *reported* rather than silently dropped (UX-07). Adding the setters is a feature, not a bug fix. |

---

## 6. Final audit table

| ID | Severity | Area | Status | Regression test |
|---|---|---|---|---|
| UX-01 | HIGH | Web bridge / config sync | **FIXED** | `testWebSliderEventSequenceApplies` |
| UX-02 | HIGH | Gameplay (WASD Race) | **FIXED** | `testWasdRaceEnglishIsCompletable` |
| UX-10 | HIGH | Build integrity (hub window) | **FIXED** | `ok_arcade_window_tests` now builds+passes |
| UX-06 | HIGH | Desktop↔web state sync | **FIXED** | `testWebBootDoesNotClobberDesktopConfig` |
| UX-09 | HIGH | Test infrastructure | **FIXED** | suite registered in runner + CMake |
| UX-07 | MEDIUM | Bridge error handling | **FIXED** | `testConfigRouteReportsRejectedKeys` |
| UX-03 | MEDIUM | UI text vs. behaviour | **FIXED** | `testHudTellsTheTruth` |
| UX-08 | MEDIUM | Chaos Lab UI | **FIXED** | `testFlexingGranularityIsConfigurable` |
| UX-04 | LOW | Arcade HUD | **FIXED** | `testHudTellsTheTruth` |
| UX-05 | LOW | Localisation | **FIXED** | `testHudTellsTheTruth` |
| — | — | Settings-wiring auditor false alarm | **CORRECTED** | negative-control verified |
| R1–R4 | LOW–MEDIUM | see §5 | **OPEN** | — |
| W1–W7 | — | see §3 | **UNVERIFIED (Windows)** | — |

---

## 7. Summary

**Confirmed fixed (10):** 5 HIGH, 3 MEDIUM, 2 LOW.
**Remaining open (4):** 1 MEDIUM (R3, suspected), 3 LOW.
**Unverified, requires Windows (7):** W1–W7.
**False alarms identified (1):** settings-wiring auditor heuristic.

**False assumptions discovered.** Four separate tests were asserting the
buggy behaviour as correct — the `applyNow` no-op (UX-01), the swallowed
WASD letter (UX-02, under the name "types ASCII 1:1"), and the
config-clobbering boot push (UX-06) — while a fifth suite (UX-09) had never
run at all and a sixth could not even be compiled (UX-10). The green suite
was, in these areas, evidence of nothing.

**Test suite status.** `tests/run_all_tests.sh --quick`: **ALL NATIVE TESTS
PASSED**, including two suites (`test_arcade_window`, `test_arcade_beta7`) that
could not be built or were never run before this audit. `web_render_test.js`,
`web_labs_test.js`, `web_progress_test.js` pass. Settings-wiring audit: 37/37
controls fully wired. `SHA256SUMS.txt` regenerated and in sync.

**Top remaining risks.**
1. **R3** — progression/AI persistence appears to have no call site; if
   confirmed on Windows, users lose all XP and achievements on every exit.
   This is the single highest-value item for the next session.
2. **W3/W4** — settings persistence and the desktop Apply flow are verified by
   reading only; both are core interaction paths.
3. **W1** — the DPI fix is a compile fix; the visual result is unvalidated.

### Release status: **NEEDS WINDOWS VALIDATION**

Not *BLOCKED*: every confirmed user-facing defect found is fixed, reproduced
before and verified after, with regression coverage that fails against the old
code.

Not *READY FOR RC REVIEW*: this audit found a **compile error in a file that
ships in the Windows executable** (UX-10) that had been sitting in the tracked
tree — which means the Windows build of this branch was, on that path, not being
compiled by anyone before now. Until a real Windows build is produced and
W1–W7 are exercised on hardware, "RC ready" is not supportable by evidence.
R3 must also be resolved, since silent loss of all user progression would be a
serious regression if confirmed.
