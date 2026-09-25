# BS-23 hypothesis log — v1.3.0-beta8fix2 (the 150 % photograph)

Status: **Phase 2, round 6** — rounds 1-5 measured every CI-reachable state
clean, window state AND pixels, both theme modes; the runner's own facts ride
in the report. The user's third-pass facts name a SCROLLING defect (dragging
the scrollbar duplicates text, Windows 10 LTSC). E8 watches the pixels WHILE
the dialog scrolls. Nothing is fixed yet.
Every line below carries the measured numbers that justify it. The layout is a
contract between the window and its children — measure both ends; trusting
either one is a new BS class.

## The field evidence (user photograph)

Default window size, 150 % DPI, tab "Cấp độ" just selected:

* **F1** — title `KieeKey v1.3.0-beta8fix1` drawn twice: once correct in the
  header, once ~150 px lower, overlapping the groupbox label of the Cấp độ page.
* **F2** — selected tab is Cấp độ (tab 8) but the page body still shows
  Thông tin (tab 4) content.
* **F3/F6** — rows clipped mid-glyph at the dialog's RIGHT edge while the
  vertical scrollbar IS visible.
* **F4** — the tab strip wraps to two rows at the default size (row 2: Chaos,
  AI, Cấp độ stretched wide) with no user resize.
* **F5** — page content starts above the visible area; the groupbox label
  touches the page top / overlaps the chrome band.

(`unikeynt.exe` warning in the screenshot is correct functionality, not a bug.)

## The CI blind spot (measured)

v1.3.0-beta8fix1 CI was 4/4 green (run 36097548730) with 4320 fuzz steps and
0 violations because **no scale pass ever entered the open path**: the 144-dpi
pass entered at 991 px client because it inherited the pass before it (683),
which inherited the first (543). Now instrumented (PR #35, run 36122792718):

```
pass @150% ... (derive: inherit 683x689 +bar 17 target 1050x1034 @dpi 144/96)
```

The target 1050×1034 is clamped by the runner's work area to 991×689 — that
is the whole 991 px mystery. The default open at 144 dpi is S(560) = 840 frame
width minus the scrollbar = **823 px client**, a geometry the old harness had
never solved.

## Round 1 verdict — the open path alone is CLEAN at every CI-reachable state

Run **36122792718** (commit 280734f, measurement-only): the new open-path
scenario reopened the dialog at each pass's scale through the app's own
WM_CREATE path and judged it with three new invariants after every operation:

```
open @96  dpi: client 543x689  page 16,114 511x521  stripRows 2 need/avail 559/503
open @120 dpi: client 683x689  page 19,139 645x493  stripRows 2 need/avail 679/633
open @144 dpi: client 823x689  page 22,169 779x453  stripRows 2 need/avail 841/763
I13 hide-set     568821 checks / 0 violations
I14 chrome band   18804 checks / 0 violations
I15 right edge    47966 checks / 0 violations
R1 handover           6 checks / 0 violations   (4320 fuzz steps, 9 scenarios)
```

Measured consequences:

* **H1 is REFUTED** as a stand-alone cause: opening the dialog at the default
  size on a 150 % scale does not, by itself, produce any F state the
  invariants can see. The strip wrapping to two rows at the default size (F4)
  is the PLAN's own arithmetic (need 841 > avail 763 at 144 dpi) — F4 is not a
  defect, it is the geometry the rest of the photograph was drawn in.
* The open path needs no settle: `stripRows == measuredRows == planRows == 2`
  at all three scales, `openSettles 0`.
* I13's 568,821 checks include the open state, a full tab sweep at the open
  geometry and a width sweep 560→1000 px at each scale: showTab's hide-set is
  intact on every state the round could reach.

So the user's state requires a driver the round did not apply. What separates
the user's machine from the CI runner, one fact at a time:

| axis | CI runner | user |
|---|---|---|
| native dpi | 96 (the 150 % layout runs under a dpi OVERRIDE) | native 144 |
| window height | clamped to 689 client (work area 1024×768) | default open wants ~950-1034 |
| dialog history | opened fresh on tab 0, audited at once | opened (possibly via tray menu onto a specific tab), lived through ticks |
| 500 ms tick | killed by the audit before it can land in the harness | alive the whole session (live rows grow twice a second) |
| dpi transitions | driven through KieeKeyProbeSimulateDpi | real WM_DPICHANGED / WM_DISPLAYCHANGE from the OS |

## Hypotheses (Phase 2)

* **H2 — tray-open sequence.** The dialog was opened ONTO a non-zero tab (the
  tray's "Thông tin & giới thiệu" opens tab 4) and the user then walked to
  tab 8. Round 1 only opened on tab 0. If F1/F2 appear, the hide-set failure
  is a function of the OPEN tab, and the double title is IDC_STAT_INFO_NAME
  (tab 4's own name label, the only other window that carries the title text)
  leaking while tab 8 is selected — the ~150 px offset matches that label's
  solved position under a two-row strip.
  *Discriminator E1*: reopen at tabs {0, 4, 8} per pass; full battery + tab
  sweep after each.

* **H3 — the live tick on a fresh two-row dialog.** The user's dialog had
  lived: the 500 ms tick rewrites live rows (uptime, diagnostics, arcade/AI
  status, coach advice) and a row that outgrows its box asks for exactly one
  reflow (`g_settingsRowGrowthPending` → `reflowSettingsLayoutPreservingScroll`
  at the end of the tick). Round 1's open scenario dropped timer messages; the
  growth reflow never landed at the open geometry. If F states appear after
  ticks/growth at the open geometry, the reflow's interaction with the
  two-row strip (or with the bar decision) is the driver.
  *Discriminator E2/E3*: land real ticks (SendMessage WM_TIMER) and a typed
  growth row at the open geometry, then assert.

* **H4 — a real DPI/display transition while open.** The user changed scale or
  moved the dialog between monitors with the dialog OPEN; the OS-suggested
  rect of a real WM_DPICHANGED (clamped to the monitor's work area, not pure
  MulDiv) lands a different frame than KieeKeyProbeSimulateDpi applies.
  F3/F6 (rows past the right edge under a visible bar) is exactly what a
  frame that did NOT get the scrollbar's 26 px subtracted looks like.
  *Discriminator E4*: send a real WM_DPICHANGED with a work-area-clamped
  suggested rect at the open geometry, up and back down.

* **H5 — the height axis (NOT REPRODUCIBLE IN CI).** The runner's work area
  caps the client at 689 px; the user's default open wants ~950+. At the
  user's height most tabs fit without the scrollbar and switching to a deep
  tab can flip the bar after the solve planned rows for the wider client —
  the bar-shown-after-solve class (F3/F6). The width sweep covers the width
  axis; the height axis stays **UNVERIFIED (Windows)** until the user
  re-tests on the real machine.

## Round 2 verdict — the tray/tick/growth/monitor-move axes are CLEAN too

Run **36123983333** (commit 0156fda): E1 opened the dialog onto tabs 0, 4 and
8 at all three scales; E2 landed two ticks on each fresh dialog; E3 grew a
live row at the open geometry and let the tick's one reflow consume it; E4
sent a real WM_DPICHANGED with a work-area-clamped suggested rect up one
scale and back. 4929 harness assertions, 9 scenarios, **0 violations**;
I13 577170/0, I14 19080/0, I15 48788/0, R1 6/0.

One new measured fact stands out — the bar decision at open time depends on
the tab the dialog opens onto:

```
open @144 dpi tab 0: client 823x689   (bar shown: 840 - 17)
open @144 dpi tab 4: client 823x689   (bar shown)
open @144 dpi tab 8: client 840x689   (bar HIDDEN: tab 8 fits, range 0)
```

Same at 120 dpi (683 vs 700). The all-tabs intent keeps the style bit, and
Windows' per-tab SetScrollInfo answer hides the bar for a tab that fits — the
flip-flop BS-22c built `settingsAdoptScrollbarVisibility` for. Every state it
passes through self-heals at the runner's clamped height: I15 found no row
past the page's right edge in any of them.

## Round 3 — the height axis made measurable (E5)

The runner's 1024×768 screen clamps every window to 689 px of client height;
the user's default open at 150 % wants ~1034 px (the pass derivation measured
`target 1050x1034`). At the user's height most tabs FIT — the bar decision,
and therefore the client width the rows are planned for, is a different state
space than anything round 1/2 could reach. E5 adds a probe-only work-area
override (`KieeKeyProbeSetWorkAreaOverride`, honored last by the solve's
refit bound) and reopens the dialog under a 1280×1600 work area at each
pass's scale: open state, tab walk, and a width sweep 560..1000 in which a
shallow and the deep tab alternate at every width — the exact sequence in
which a bar that appears for the deep tab narrows the client under rows
planned for the wide one (the F3/F6 class).

## Round 3 verdict — the height axis is CLEAN too

Run **36134839800** (commit 7e1327a): E5 reopened under a 1280×1600 work
area at all three scales. The dialog's natural full height measured **749 px
of client** (not 1034 — the pass's 1034 was the clamped 689 scaled up, not a
content depth), with the bar ON and range 0/47/227 at 96/120/144 dpi. The
full-height open state, a nine-tab walk, and a width sweep 560..1000 with a
shallow and the deep tab alternating at every width: **0 violations**
(I13 605847/0, I14 20028/0, I15 52703/0, 5166 assertions, 9 scenarios).

## The user's facts (Phase 0, second pass — asked after round 3)

* **The breakage PERSISTS across close-and-reopen** (same state every time).
* **A tab click produces it**: "bấm tab thì bị đè. Mất nội dung. Nhất là khi
  bấm tab 'Cấp độ'." — pressing tabs overlaps content and loses content,
  worst on tab 8 (Cấp độ).
* **Single monitor, 150 % scale from boot.** No multi-monitor, no mid-session
  scale change.
* More photographs come with the next prompt.

Persistence across reopen rules out transient races and points at a state
that is DETERMINISTIC on this machine — the dialog rebuilds the same broken
frame from the same inputs every time. The tab-click trigger plus "đè"
(overlap) plus "mất nội dung" (lost content) is the signature of a
**paint-level** defect: the window state can be perfectly correct (every
window-state invariant green, as rounds 1-3 measured) while the PIXELS the
user sees are a mixture of frames — exactly what the window-state invariants
cannot see and what `checkStalePixels` was written to catch ("pixels of
moved, hidden or painted-through controls were left behind"). That check has
only ever run at the PASS geometry (991 px at 150 %); the photograph was
taken at the DEFAULT-OPEN geometry (823 px, two-row strip).

## Round 4 — E6, the pixel truth at the photographed geometry

After the default open at the pass's scale, walk the nine tabs and run the
stale-pixel audit on each: capture the screen, force the app's full repaint,
capture again — any pixel that moved is content the desktop held that the
app does not draw. ## Round 4 verdict — the pixels at the photographed geometry are CLEAN too

Run **36137870873** (commit 882997e): E6 ran the stale-pixel audit on the
default-open dialog at all three scales, tab by tab — screen capture, forced
full repaint, capture again. Screen checks 27 → **54** (+27 open-geometry
audits), **0 findings**. The desktop holds exactly what the app draws, at
823 px under a two-row strip, after every one of the nine tab switches.

Four rounds, every axis the harness can drive — open path, tray-open tabs,
landed ticks, growth reflows, real WM_DPICHANGED, full height, width sweeps
with the bar flipping, the pixel truth: **0 violations**. The beta8fix1 tree
is coherent in every state this runner can produce.

## Round 5 — the runner itself is the last unmeasured axis (E7 + host facts)

What still separates the runner from the user's machine is the runner:
Windows Server 2022 CI image vs. the user's desktop Windows at 150 %. The
tab control's item layout and paint are a negotiation with comctl32/uxtheme,
and **visual styles change both** — classic vs. themed mode lays the strip
out differently (padding, row height), and the app's row-count decision
trusts its own measurement against the control's. Round 5:

* the report now carries the runner's facts: OS build via RtlGetVersion,
  IsAppThemed/IsThemeActive, DWM composition (`host:` entry note + JSON
  `host` key),
* E7 reopens the dialog at the pass's scale and opts that window tree OUT of
  visual styles (`SetWindowTheme(hwnd, L" ", L" ")`, the documented
  opt-out), forces a re-layout, and judges the battery + the stale-pixel
  audit tab by tab — so whichever mode the runner itself is in, the OTHER
  one is measured.

If E7 turns red, BS-23b belongs to the theme-mode negotiation and the fix is
a settle that trusts the control's own layout answer. If it stays green, the
measurement record is complete (five rounds, every reachable axis clean) and
the release proceeds on mechanism with UNVERIFIED (Windows) until the user's
re-test on the real machine — with the hypothesis log as the evidence trail.

## Round 5 verdict — theme mode is NOT the discriminator; the user's facts deepen

Run **36139368885** (commit ab73c6c): E7 ran the classic-mode reopen at all
three scales — geometry identical to themed in every number
(client 823×689, page 22,169, stripRows 2 @150), pixel audit clean (screen
checks 54 → **81**, 0 findings). Theme mode is not the axis. The report now
carries the runner's own facts: `host: win 10.0.20348 appThemed 1 themeActive 1
dwm 1 native dpi 96`. Five rounds, every axis the harness can drive on this
runner, window state AND pixels: **0 violations**.

Then Phase 0, third pass — the user's facts sharpened the picture:

* the window is NOT resizable (the dialog ships without `WS_THICKFRAME`) — so
  "kéo" in the user's words is the SCROLLBAR thumb, not a window edge;
* **"kéo thì các chữ bị nhân bản" — dragging the scrollbar DUPLICATES text**,
  on **Windows 10 LTSC 21H2** (build 19044 — the runner is Server 2022,
  build 20348), x64.

That is a SCROLLING defect, and it is a named one: the BS-13/BS-16a comment
block in `main.cpp` quotes this exact symptom — *"the stale copies of text
left behind while scrolling ('chữ bị duplicated')"* — and its fix was the
full-dialog invalidation after the children move. Every pixel audit so far
(E6/E7) ran at REST; none ever watched the dialog WHILE it scrolled.

## Round 6 — the pixel audit WHILE scrolling (E8)

First E8 attempt reopened on tab 8 at the DEFAULT geometry: range 0, drag
skipped — and the reason is itself a measurement: the bar's range latches
PER TAB at open (round 2's flip-flop), and tab 8 latches 0. The range only
exists at the FULL-HEIGHT geometry (E5's work-area override: range 227 @150,
tab 0) — exactly the "bar shown with overflowing content" state of the
photograph.

E8 final design: give the refit the same tall work area, reopen on TAB 0
(the tab E5 proved carries a range), drive the app's OWN scroll channel
(WM_VSCROLL line/page/bottom/top — the scrollbar's internal track position
cannot be synthesized, but every code ends in the same tail
`applySettingsScrollOffset`), walk the offset out and back with a pixel
audit MID-SCROLL at the deepest point and another after the trip. Then the
user's exact sequence: switch to tab 8 (Cấp độ), drag again, audit again.
Anything the forced full repaint moves is content the desktop still holds
from a mid-scroll frame — ghost text at an old offset. If E8 turns red the
defect is in the scroll repaint path; if it stays green the measurement
record is complete and the release proceeds on mechanism with UNVERIFIED
(Windows).
