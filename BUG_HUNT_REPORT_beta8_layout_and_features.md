# Bug Hunt Report — KieeKey v1.3.0-beta8
## Layout truth, diagnostics readability, Chaos-Lab DPI, and the v1.3.0 data-loss defect

**Audit date:** 2026-09-22
**Baseline commit:** `e9e5009` (`main`, = v1.3.0-beta7, Windows file build 1.3.0.8)
**Branch:** `arena/01a0c8ba-kieekey`
**Scope:** the tester's beta7 report, item by item — text clipped and impossible to
reach, labels that outgrow their boxes when the value changes, the Chẩn đoán tab
that cannot display its own report, combo drop-downs that cover neighbours, the
Chaos Lab at non-100 % DPI, and the v1.3.0 progression/AI data loss — each with a
reproduction, a regression test that is RED on the pre-fix tree, and an explicit
verification label.

**Verification labels.** `[VERIFIED]` = reproduced and fixed with a regression
that is red before / green after. `UNVERIFIED (Windows)` = the fix is in place and
a Windows-only mechanism is involved (real HWNDs, real DPI, real `SendInput`), so
only the manual checklist or the CI UI probe can close it.

---

## 0. Method

Nothing here is a code-reading guess. Every item was reproduced first:

* `scripts/audit_layout.py` (static, source-derived geometry) was **extended with
  four new checks** and every new check was **seed-verified red** through
  `tests/verify_audit_seeds.py` — a check that has never failed is not evidence;
* the pure geometry model behind the scroll bars was extracted into
  `src/app/DialogLayout.hpp` and probed with a 6-seed harness
  (`tests/test_dialog_layout.cpp`);
* the report text layer was extracted into `src/app/DiagReportText.hpp` and
  probed (`tests/test_diag_report_text.cpp`);
* the persistence rules were extracted into `src/app/PersistPolicy.hpp` and the
  engines were round-tripped through a real save → restart → load
  (`tests/test_progression_persist.cpp`);
* every Windows-only source was cross-compiled locally with zig
  (`-target x86_64-windows-gnu`, `-Wall -Wextra`) **and** a negative control was
  run to prove the check can fail;
* a full local gate (`tests/run_all_tests.sh --quick --jobs=2`) had to stay green
  at the end of every stage.

---

## 1. Confirmed bugs

### BS-01 — The scroll fallback did not scroll: content was invisible **and unreachable**
| | |
|---|---|
| **Severity** | **HIGH** |
| **Area** | Settings dialog scroll model (`src/app/DialogLayout.hpp`, `main.cpp`) |
| **Status** | **FIXED — `[VERIFIED]`** |

**Symptom.** The tester's original words: *"chữ bị che / không kéo"* — text is
covered and the scrollbar cannot be dragged. Controls below the tab viewport were
authored (and painted) but the scrollbar never became enabled, so on a small
window or a large font the lower rows of a tab simply could not be reached.

**Root cause.** `ScrollMetrics` mixed two different units (device pixels for the
viewport, "dialog units" for the page) and derived `rangeMax = contentPx` without
the mandatory −1, so for a page that exactly filled the viewport Windows showed a
scrollbar that scrolled by one pixel, and for a page that overflowed it never
reported a usable range at all.

**Fix.** One unit everywhere: `pagePx = max(1, viewport)`,
`maxTravelPx = max(0, content − viewport)`, `contentPx = pagePx + maxTravelPx`,
`rangeMaxPx = contentPx − 1`, `enabled = maxTravelPx > 0`.

**Regression.** `tests/test_dialog_layout.cpp` — 14 checks over the six seeds
(fits / exactly-fits / one-pixel overflow / tall content / zero-height viewport /
huge content). RED on the pre-fix header (missing members, wrong range), green now.

### BS-02 — Labels that change at runtime outgrow the rectangle solved at creation
| | |
|---|---|
| **Severity** | **HIGH** |
| **Area** | Settings dialog runtime text (`src/app/main.cpp`) |
| **Status** | **FIXED (Linux-side) — `UNVERIFIED (Windows)` for the final pixels** |

**Symptom.** Rows that are one line at creation (application version, the live
gate, every diagnostics result line) and rows that grow (engine status, arcade
status, AI stats, coach advice) were clipped mid-word once the runtime wrote a
longer value: the solver only ran at dialog creation.

**Fix.** Two mechanisms, both measured with the real font at runtime:

* `markOneLineRow()` — one-line rows get `SS_ENDELLIPSIS` plus a tooltip carrying
  the full text (`g_rowTip` / `ensureRowTooltip` / `setRowTooltip`, destroyed on
  `IDCANCEL` and `WM_DESTROY`), so a value can never be silently cut;
* `refreshGrowingRow(ctl, text, maxGrowPx)` — `WM_GETFONT` → `DrawTextW(DT_CALCRECT)`
  → grow **only** if the wrapped height grew (never shrink, never shrink back the
  user's scroll), capped per row: `IDC_STAT_INFO_STATUS +8`, `ARCADE_STATUS +4`,
  `AI_STATS +5`, `COACH_ADVICE +60`.

**Regression.** `scripts/audit_layout.py` `audit_single_line_policy` (a one-line
row that cannot fit must declare `SS_ENDELLIPSIS` or a grow marker) plus two
seed-verified violations in `tests/verify_audit_seeds.py`.

### BS-05 — A combo box's invisible drop-down window covered lower-z-order siblings
| | |
|---|---|
| **Severity** | **MED** |
| **Area** | Settings dialog control creation (`src/app/main.cpp`) |
| **Status** | **FIXED — `[VERIFIED]`** |

**Symptom.** Some controls (e.g. the radio row under the code-table combo) did not
respond to clicks; only the creation order kept them usable.

**Root cause.** For `CBS_DROPDOWNLIST` the height passed to `CreateWindowExW` is
the **drop-down list height**. Twelve combos were authored 120–200 units tall, so
each one's *window* covered up to 175 px of the tab below the 25 px the user sees.

**Fix.** Every combo is now authored **closed** (25 units) and asks for its list
height afterwards through the new `applyComboDropHeight(combo, dropDownPx)`
(`CB_GETITEMHEIGHT` + `CB_GETCOUNT` → `CB_SETMINVISIBLE`, clamped 1..count) — the
invisible-area class is gone, not merely out-ordered.

**Regression.** `audit_layout.py` gained `combo_window` (a `CBS_DROPDOWNLIST`
authored taller than one row is a finding; a closed one without
`applyComboDropHeight()` is a probable finding) and the `under_combo` z-order
check, both seed-verified. The pre-existing `under_combo` seed was re-pointed at
the real beta7 shape (a 200-unit combo) so the old class stays covered.

### BS-06 — Chaos Lab rescaled the font on DPI change but never the child rectangles
| | |
|---|---|
| **Severity** | **HIGH (Lab unusable at 125/150 %)** |
| **Area** | `src/app/ChaosLabWindow.cpp` |
| **Status** | **FIXED — `[VERIFIED]` for the source contract, `UNVERIFIED (Windows)` for pixels** |

**Symptom.** Opening the Lab on a scaled monitor, or dragging it to one, produced
"chữ bị đè" — the glyphs grew inside unchanged 96-DPI boxes: the "Mẹo: …" note
wrapped to three lines in a two-line box and checkbox labels ran past their rects.

**Root cause.** `WM_DPICHANGED` recreated the Segoe UI face and adopted the
suggested window rect — but nothing ever moved the children, and the window was
never guaranteed to be at least as large as the scaled 760×880 design.

**Fix.** Every child is created through the single `create()` wrapper, which
records its authored 96-DPI rect in `Impl::ChildRect` (`impl.children`).
`applyLabLayout()` re-positions all of them with `MulDiv` from that table, and
`fitLabWindowToDesign()` grows the window to the scaled design when it is smaller
(never shrinks a window the user enlarged). Both are applied at open **and** in
`WM_DPICHANGED`, next to `applyLabFont()`.

**Regression.** `scripts/audit_chaos_lab.py` gained layer 4 — exactly one raw
`CreateWindowExW` outside the wrapper (the top-level window), the wrapper records
every child, and the `WM_DPICHANGED` arm re-applies font + rects + window fit.
RED on the beta7 source (4 findings), green now; plus the 100/125/150 % text-fit
sweep in `audit_layout.py`.

### BS-07 — Arcade Hub footer hint ran under the FPS counter; the counter ignored DPI
| | |
|---|---|
| **Severity** | **MED** |
| **Area** | `src/app/ArcadeWindow.cpp` |
| **Status** | **FIXED — `[VERIFIED]` for the geometry, `UNVERIFIED (Windows)` for pixels** |

**Symptom.** With a long hint (the Backspace-recovery message) the footer text
disappeared under the FPS counter, and at 125/150 % the counter did not scale.

**Root cause.** Two functions, two formulas: `drawChrome()` drew the hint with a
bare `TextOutW` and no measurement; `presentFrame()` drew `"%.0f FPS"` at the
literals `width − 76, height − footer − 22` with `fontFor(13)`.

**Fix.** New portable `src/app/ArcadeChromeLayout.hpp`
(`ok::arcadechrome::planFooter` + `ellipsizeToWidth`): both call sites ask it for
their band, the counter is right-aligned inside the reserved band at a DPI-scaled
size, and an over-long hint is ellipsized to the band before it is drawn.

**Regression.** `tests/test_arcade_chrome_layout.cpp` — 5 tests / 177 checks:
constants mirror `ArcadeWindow.hpp`; hint/meta/counter bands are pairwise disjoint
and inside the window at 100/125/150/200 % × 640/1024/1600; the longest real hints
overflow the band and ellipsize back into it; short hints are untouched; the
counter stays above the footer. RED probe `/tmp/redprobe/old_chrome` reproduces
the beta7 geometry violating the invariant **10 times**.

### BS-08 — The worst-case note rows (drivers of tab overflow) were unpinned
| | |
|---|---|
| **Severity** | **LOW** |
| **Area** | Tab 0 `IDC_STAT_OUT_NOTE` + three sibling notes |
| **Status** | **FIXED — `[VERIFIED]`** |

`IDC_STAT_OUT_NOTE` is ~145 characters in a 460×34 box (three lines at 96 DPI) and
is the main reason tab 0 scrolls. It is legal only while it fits, so it and the
three sibling notes (`METHOD_HINT`, `CHAOS_WARN`, `LIVE_HINT`) are now pinned by
name: the audit fails if one is renamed away or stops fitting at 100/125/150 %.
Two seeds prove the check can fail.

### BS-09 — The bottom button row collapsed to x=0 whenever the dialog grew
| | |
|---|---|
| **Severity** | **HIGH** (found by the new CI probe, invisible to every static audit) |
| **Area** | Settings dialog: `IDC_BTN_TOGGLE`, `IDOK`, `IDCANCEL`, `IDC_BTN_APPLY` |
| **Status** | **FIXED — `[VERIFIED]`** (portable model + test; pixels: manual M2) |

`solveSettingsLayout()` anchors the always-visible bottom row by moving it DOWN
with the refit's client delta. The apply loop called

```cpp
::SetWindowPos(c, nullptr, 0, rc.top + fit.clientDelta, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
```

`SWP_NOSIZE` suppresses cx/cy — it does **not** suppress the move: X and Y are
applied unless `SWP_NOMOVE` is passed. So every control below the tab control
(`rc.top >= rcTab.bottom - S(4)`, i.e. y=580 ≥ 572−4) jumped to **x = 0** the
moment the refit grew the window, stacking the four buttons on the left edge and
on top of each other.

Why nothing caught it before: `scripts/audit_layout.py` models rectangles, not
`SetWindowPos` flags; the condition (`fit.clientDelta != 0`) only fires when the
real font on the real monitor needs a taller window; and the row keeps working
when the dialog already fits. The **first CI run of the CA-03 probe** found it:
`id 550/2/517 at 0,647` overlapping by `76x30`/`80x30` px, on all nine tabs.

Fix + guard: the decision now lives in the portable model
(`ok::layout::bottomRowMove()`, `src/app/DialogLayout.hpp`) so it is unit-tested
— the row moves down by the delta and keeps its authored X — and `main.cpp`
applies the model's rectangle. Evidence:

* `tests/test_dialog_layout.cpp` → `testBottomRowMovesDownOnly()` (row keeps X
  and the authored gaps; header above the tab bottom never moves; delta 0 is a
  no-op; the `>= tabBottom - slack` edge is inclusive).
* **Seed verified**: forcing `m.rect.x = 0` (the beta7 behaviour) makes the suite
  abort with ``Assertion `m.rect.x == r.x' failed``; restored → green.
* Baseline proof: the same call shape is present in `git show e9e5009:src/app/main.cpp`
  (lines 4412-4414), i.e. this is a **beta7 defect, not a beta8 regression**.

### BS-10 — A two-row tab strip hid the top of the page
| | |
|---|---|
| **Severity** | **HIGH** (found by the CA-03 probe; the "chữ bị che" class) |
| **Area** | Every tab page: group boxes at y=100, labels at y=110 vs `disp.top=114` |
| **Status** | **FIXED — `[VERIFIED]`** (model + test; pixels: manual M1/W2) |

The nine tab labels wrap to a **second row** when the dialog is narrow or the
font is larger (`TCS_MULTILINE`, planned by `ok::layout::planTabs`). The display
rectangle then starts one row lower — the probe measured `page=[16,114,510,635]`
on the CI runner — while the authored page rectangles were solved for a
single-row strip. Result: the top 4–14 px of every page (a group box's title row,
a full label) was painted **under the tab labels**.

Fix: `ok::layout::pageTopShiftPx(authoredTopPx, viewportTopPx)` — a page whose
first control is above the display rectangle shifts down so it lands at/below it;
a page already at/below shifts by 0, so re-solving is a no-op (the solver's
"never move up or sideways" contract is intact, and the shift feeds the same
`autoFit` → refit → scroll arithmetic as everything else).

Evidence:

* `tests/test_dialog_layout.cpp` → `testPageTopShiftKeepsContentBelowTheTabStrip()`
  (14/4 px for the measured 100/110 vs 114; 0 for the one-row strip; idempotent).
* **Seed verified**: forcing `return 0;` makes the suite abort with
  ``Assertion `pageTopShiftPx(100, 114) == 14'``; restored → green.
* Probe evidence (run 3): `tab 0 [outside_page] id 555 at 24,100 494x112`,
  `tab 4 [outside_page] id 558 at 28,110 400x34`, … on all nine tabs.

### BS-11 — The runtime solve never grew a single label (case-sensitive class names)
| | |
|---|---|
| **Severity** | **HIGH** (the whole runtime half of the layout model was inert) |
| **Area** | `solveSettingsLayout()`: `isStatic` / `isGroupBox` detection |
| **Status** | **FIXED — `[VERIFIED]`** (audit + seed + probe evidence) |

```cpp
const bool isStatic = (clsLen == 6 && wcscmp(cls, L"STATIC") == 0);   // never true
const bool isGroupBox = (clsLen == 6 && wcscmp(cls, L"BUTTON") == 0) && (style & BS_GROUPBOX) == BS_GROUPBOX;
```

The predefined Win32 classes report **mixed case** from `GetClassNameW`
("Static", "Button", "Edit" — the CI probe's own findings printed `(Static)` and
`(Button)`), so both comparisons were false for **every** control. Consequences:
`ControlSpec::growable` was never true → no label was ever grown to its measured
height at run time, and `ControlSpec::groupBox` was never true → no group box was
ever stretched to contain its grown children. The dialog only ever looked correct
because the *authored* rectangles had been tuned to satisfy the static audit's
font model — which is a few pixels short of the real font, so the two deepest
labels clipped (`IDC_STAT_LIVE_HINT`: box 96 px, real font needs 102; the CI
annotation read `wraps to 102px (app solver says 102) in a 96px box`).

Why nothing caught it: `tests/test_dialog_layout.cpp` builds `ControlSpec`
objects by hand (it sets `growable`/`groupBox` itself), and
`scripts/audit_layout.py` models the authored geometry — the detection of the
real HWND class is only exercised on Windows, i.e. by the CA-03 probe.

Fix and guard:

* `::lstrcmpiW(cls, L"STATIC")` / `::lstrcmpiW(cls, L"BUTTON")` (case-insensitive,
  the documented comparison for class names).
* **New static check** `audit_win32_class_matching()` (CA-01e) in
  `scripts/audit_layout.py`: any case-sensitive comparison of a GetClassName
  result against an ALL-CAPS predefined class literal is a hard finding, with the
  reason in the message. Seed registered as
  `CA-01e  GetClassNameW compared against an ALL-CAPS class literal` →
  `tests/verify_audit_seeds.py` reports 10/10 seeds caught.
* Runtime evidence: the probe prints the app's own measurement next to the
  rectangle it was applied to, so a future regression reads
  `app solver says 102 … in a 96px box` again instead of staying silent.

### DS-01/02/03/05 — The Chẩn đoán tab could not display its own report
| | |
|---|---|
| **Severity** | **HIGH** |
| **Area** | `src/app/main.cpp` + new `src/app/DiagReportText.hpp` |
| **Status** | **FIXED — `[VERIFIED]`** |

**Symptom.** "Xuất báo cáo" wrote a file; "Sao chép" put text on the clipboard;
the user could not READ the report inside the app. The quick check reported a raw
machine token (`engine|backspace|counters|…`) that means nothing to a tester.

**Fix.** A read-only, scrollable `EDIT` pane (`IDC_EDIT_DIAG_REPORT`, 462×184,
`ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL`) fed by the **same builder the export
uses** (`apptext::diagReportPayload(Diagnostics::report(40), live-gate line)`), so
the pane and the file cannot disagree; **Xem báo cáo** refreshes it, **Mở file**
opens the last export through `ShellExecuteW` (rc > 32 checked), and the quick
check now prints a Vietnamese item list from the same token mapping (6/6 pass,
partial failures listed with ` · `). The beta7 truth markers
(`emit-chain`, `process-resolution`, `display-metrics`, provenance) survive in
both the pane and the export.

**Regression.** `tests/test_diag_report_text.cpp` — 7 tests: every token mapped,
ĐẠT 6/6, partial-failure ordering, unknown token surfaced (never swallowed), pane
bytes == export bytes, truth markers present, LF→CRLF idempotent.

### FT-01 (P0) — Progression and the learned AI profile were lost on every exit
| | |
|---|---|
| **Severity** | **HIGH — silent data loss in a shipped v1.3.0 feature** |
| **Area** | `src/app/main.cpp` + new `src/app/PersistPolicy.hpp` |
| **Status** | **FIXED — `[VERIFIED]`** |

**Symptom.** XP, level, achievements and the AI rival's learned profile reset on
every restart. The v1.3.0 progression feature was, in practice, per-session only.

**Root cause.** `Progression::saveToFile/loadFromFile` and
`AiRival::serializeProfile/deserializeProfile` have existed (and are unit-tested)
since v1.3.0 — **nothing under `src/app/` ever called them.** The gates could not
see it: they test the engines, not the application.

**Fix.** `%APPDATA%\KieeKey\progression.dat` + `aiprofile.dat`, written by
`saveProgressionAndAi()` on the clean-exit sweep (`WM_ENDSESSION`) and on clean
destroy (`WM_DESTROY`), and by `maybeSaveProgressionThrottled()` (30 s, skip when
nothing changed) as the first action of the UI tick so a crash costs at most 30 s;
loaded by `loadProgressionAtBoot()` immediately after `loadMacros()`, fail-open —
a corrupt file is reported once with a `MessageBoxW` and the session starts fresh.
The AI profile is written **only while the user is opted in**
(`ok::apppolicy::shouldPersistAi(isOptIn())`).

**Regression.** `tests/test_progression_persist.cpp` (29 checks: file names, path
join, opt-in gate, throttle policy, save → restart → load round trip, corrupt →
fail-open) **plus** the source gate `scripts/audit_feature_persistence.py`, which
is RED with **11 findings** on the pre-fix tree (`--repo=` a `git show` copy of
`main.cpp`) and green now.

### FT-02 — "Gõ chữ Flexing ra app" failed silently
| | |
|---|---|
| **Severity** | **MED** |
| **Area** | `src/app/ChaosLabWindow.cpp` + new `src/app/FlexSendOutcome.hpp` |
| **Status** | **FIXED (Linux-side) — `UNVERIFIED (Windows)` for the real activation path** |

**Symptom.** Pressing the button did nothing and said nothing. Three different
real causes (no external target remembered / Windows refused the activation / the
emitter rejected the chunk) all returned a bare `0`, exactly like "queued".

**Fix.** The outcome is now an enum with a Vietnamese reason per case
(`ok::flexsend`), a new status row in the Lab shows it, and a refused activation
raises a **one-time** dialog per session with the fix ("click the target app
first, run KieeKey at the same elevation"). Fail-closed behaviour is unchanged —
the emitter still never types into a window that is not the requested one — and
the mid-injection focus loss now stops with `TargetLost` instead of silence.

**Regression.** `tests/test_flex_send_outcome.cpp` (7 tests / 94 checks: all eight
reasons non-empty, pairwise distinct, five classified as failures, only the focus
refusal alerts, every failure carries dialog advice, every status line fits the
authored 720-unit row) + layer 4 of `scripts/audit_chaos_lab.py` (the reason table
is consulted at the reporting sites and no `(void)typeIntoFocusApp` remains) —
RED on the beta7 source.

### FT-03 / FT-05 (risk R1) — F9 stole the arcade key, and the gate note could drift
| | |
|---|---|
| **Severity** | **MED / LOW** |
| **Area** | `src/app/main.cpp` |
| **Status** | **FIXED — `[VERIFIED]`** |

F9 (bare) still toggles the tone style, but it now checks `g.arcadeOwnsKeyboard`
— a mirror the **UI timer** publishes from
`ArcadeManager::isConsumingKeyboard()`. This matters architecturally: the
producer thread may not call the arcade singletons, and
`scripts/check_input_isolation.py` fails the build if it does (it caught the first
implementation of this guard). The Live-Effects tab's gate row is on the BS-02
fit path, and the "+1,8 ns/ký tự" note is pinned to the figure in
`docs/PERFORMANCE.md` by `scripts/audit_live_effects_truth.py` (RED on beta7 with
4 findings).

### FT-04 (phase2-R3) — the web lab's "N" input: resolved, not removed
| | |
|---|---|
| **Severity** | **LOW** |
| **Area** | `web/labs.js`, `web/index.html`, `tests/web_labs_test.js` |
| **Status** | **FIXED / CLOSED — `[VERIFIED]`** |

The question was whether the web lab's `flexN` input drives the engine or is
decoration. It drives it: `POST /api/preload` carries `{granularity, nChars}` and
`ArcadeServer` calls `FlexingGame::setGranularity(gran, nChars)`
(`src/core/ArcadeServer.cpp:616`), the same engine call the desktop Lab's
granularity combo makes (`ChaosLabWindow.cpp:263-270`). `flexN` is therefore the
free-form counterpart of the desktop's fixed "N ký tự" choices and is **kept**.
The test now asserts that N is taken from the input (not a hardcoded default), that
editing N re-sends it live, that the passage is preserved, and that the HTML bounds
(min 1 / max 64) match the engine's own clamp (`Arcade.cpp:3084`).

---

## 1a. CA-05 — what the probe had to learn before it could see the user's screen

The user's report from a real Windows desktop ("chữ bị đè còn nhiều hơn cả lúc trước
nữa… khi kéo thì chữ loạn lên") arrived while the UI probe was **green** (184
controls, 1764 checks, 0 findings, DPI 96). Both facts were true, and the gap between
them was the probe's own model:

| what the probe assumed | what decides the user's screen | consequence |
|---|---|---|
| `GetWindowRect` is the control | the WINDOW REGION is the control: the page children are direct children of the dialog and "scroll" by moving + `SetWindowRgn`-clipping to the tab viewport | a hidden or mis-clipped control measured perfectly; a region expressed in the wrong space cannot be detected at all |
| every visible control may be compared with every other | exactly ONE page is on screen: the other tabs' controls must be invisible (and region-hidden controls are not "visible" in the sense that matters) | two pages' controls could overlap and the probe would still call it a layout defect of neither |
| the page always fits | the scroll fallback engages whenever the work area cannot show the solved page — i.e. on the high-DPI laptops where users complain, never on the runner's large 96-dpi desktop | the entire scrolling path (moving children, clipping, restoring) was NEVER executed in CI |
| DPI 96 is the layout | 125 % / 150 % change the font-to-box ratio, the tab strip row count, the display rectangle and the refit budget | "virtual 150 % stays MODELLED" was doing the work of a real measurement |

CA-05 closes those four gaps in `tools/ui_probe/ui_probe.cpp` + `src/app/main.cpp`
(probe-only hooks `KieeKeyProbeTabOfControl`, `KieeKeyProbeSimulateDpi`; the DPI
rescale path is shared with `WM_DPICHANGED` instead of being duplicated):

* every geometry check runs on the **effective rectangle** = window rect translated by
  the region's bounding box, clipped to the page, and skipped entirely when the
  control is not on screen;
* `wrong_page` / `page_hidden` prove that exactly one page is on screen;
* `region` compares the applied region with the one the page allows, so a
  region-space or stale-region bug is a finding instead of a mystery screenshot;
* `group_overlap` reports a group box covering a control it does not contain — the
  "text under a grey band" class that both `findOverlaps()` and the old probe
  deliberately skipped;
* the **scroll path** is exercised (`WM_VSCROLL`/`SB_THUMBTRACK` at 0/25/50/75/100 %
  of the travel) and re-audited at every step, which is where "khi kéo thì chữ loạn
  lên" lives;
* the whole audit runs a second time at **150 %** through
  `KieeKeyProbeSimulateDpi()` (the app's own `applySettingsDpiScale()` + solve), so
  the scale the user reports is measured rather than modelled.

## 2. Verification layers

| Layer | What it proves | Result |
|---|---|---|
| Local gate `tests/run_all_tests.sh --quick --jobs=2` | ~50 native targets + 14 audits + 4 node suites + SHA256SUMS | **ALL GREEN** (see §4) |
| New portable suites | BS-01 (14), BS-07 (177), DS-01/02 (7), FT-01 (29), FT-02 (94 checks) | green |
| Seed-verified audits | `audit_layout` (9 seeds), `audit_chaos_lab` (4 layers), `audit_feature_persistence` (11 RED findings pre-fix), `audit_live_effects_truth` (4 RED pre-fix) | green |
| Cross-compile (`zig c++ -target x86_64-windows-gnu -Wall -Wextra`) | every Windows-only edit compiles; negative control fails as expected | green |
| Windows CI `windows-2022` (x64/ARM64/ARM64EC, MSVC `/W4 /WX`, ctest) | the REAL toolchain | **GREEN** (run 35726539425: Build + 8/8 ctest on x64/ARM64/ARM64EC) |
| Windows CI UI probe (`kieekey_ui_probe`, x64) | real HWNDs, real font metrics, per-tab screenshots, scrollbar truth | **executed** — runs 1..5 found **BS-09, BS-10, BS-11** and 10 probe-side false-positive classes; each fixed |
| Manual checklist W1–W7 / M1–M7 | real DPI, tray, hook, real `SendInput` into external apps | **pending (user)** |

---

## 3. UNVERIFIED — requires physical Windows validation

* Pixel-level results of BS-02/BS-06/BS-07 (the *mechanisms* are pinned by tests,
  the final rendering needs a real window manager and font).
* FT-01 across a real restart, and the corrupt-file `MessageBoxW` path.
* FT-02's activation-denied path (`SetForegroundWindow` refusal is a Windows
  policy decision) and the one-time dialog.
* The UI probe itself has never executed (it is a new CI capability).

## 4. Remaining open risks (reported, not fixed)

* **R2** — one-frame cosmetic latency in the live-effects overlay: accepted,
  documented in the release notes.
* **R4** — geometric 90°/270° rotations are render-only by design; the Lab's
  honesty line already says so (UX-07, unchanged).
* **UI probe at virtual 150 %** — the probe reports the DPI it really measured; the
  125/150 % arithmetic stays modelled by `audit_layout.py` and manual M1.

## 5. Final audit table

| Item | Severity | Status | Evidence |
|---|---|---|---|
| BS-01 scroll model | HIGH | FIXED | `test_dialog_layout` 14/14, RED pre-fix |
| BS-02 runtime growth | HIGH | FIXED | `markOneLineRow`/`refreshGrowingRow`, 2 seeds |
| BS-03 short labels | LOW | FIXED | folded into BS-02(a); `audit_single_line_policy` |
| BS-04 group containment | LOW | FIXED | `outside_group`, seed-verified |
| BS-05 combo drop-downs | MED | FIXED | `combo_window` + `under_combo`, 2 seeds |
| BS-06 Lab DPI | HIGH | FIXED | `audit_chaos_lab` layer 4, RED pre-fix (4) |
| BS-07 footer/FPS | MED | FIXED | `test_arcade_chrome_layout` 177 checks, RED probe (10) |
| BS-08 worst-case notes | LOW | FIXED | `pinned_row` pin, 2 seeds |
| BS-09 bottom row x=0 | HIGH | FIXED | `bottomRowMove()` + `testBottomRowMovesDownOnly`, seed RED |
| BS-10 two-row tab strip hid page tops | HIGH | FIXED | `pageTopShiftPx()` + `testPageTopShiftKeepsContentBelowTheTabStrip`, seed RED |
| BS-11 class names compared case-sensitively | HIGH | FIXED | `lstrcmpiW` + new audit `class_case` + seed (10/10) |
| DS-01 report pane | HIGH | FIXED | `test_diag_report_text` 7/7 |
| DS-02 token mapping | MED | FIXED | same suite (6/6 + partial order) |
| DS-03/04/05 | MED/LOW/INFO | FIXED | pane == export; hook counters under `Level::Off` |
| CA-01 a–d | P1 | DONE | 9 seed-verified checks, `--strict` green |
| CA-03 UI probe | P1 | DONE | CI step on x64; found BS-09/10/11 (3 RED runs), then green over 184 controls / 1764 checks |
| CA-05 probe: effective rects, one page at a time, the scroll path, 150 % | P1 | DONE | probe audits the visible rectangle (window rect ∩ region ∩ page), drives the real `WM_VSCROLL` path through the whole travel, and re-audits at 150 % through the app's own DPI-change path |
| FT-01 persistence | HIGH | FIXED | 29-check suite + 11-finding RED audit |
| FT-02 silent Flexing send | MED | FIXED | 94-check suite + audit layer 4 |
| FT-03 gate truth | MED | FIXED | `audit_live_effects_truth` |
| FT-04 web parity | LOW | CLOSED | `web_labs_test.js` 35 checks |
| FT-05 R1 F9 guard | LOW | FIXED | isolation gate + mirror atomic |
| FT-06 eight games | INFO | REGRESSION ONLY | all `test_arcade*` green |

## 6. Release status: **CI GREEN — READY FOR THE MANUAL CHECKLIST**
