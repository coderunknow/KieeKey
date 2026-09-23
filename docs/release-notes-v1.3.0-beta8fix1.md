# KieeKey v1.3.0-beta8fix1 — "mất nội dung": the settings page that lost its content

Windows file version **1.3.0.10** · UI string **1.3.0-beta8fix1** · built from the
`main` tree after `v1.3.0-beta8` (the fix is a new commit, not a re-tag).

> **Trạng thái: CHƯA xác nhận trên máy của bạn.** Bản này sửa nguyên nhân gốc đã
> được tái hiện **trong CI** (xem bên dưới), nhưng hành vi trên máy Windows của
> bạn chỉ có bạn xác nhận được — hãy dùng `TESTING.txt` đi kèm.

## What the tester reported

> "Cài đặt bị mất nội dung" — the page area of the Settings dialog goes blank and
> the vertical scrollbar disappears. Scrolling (wheel, arrows, dragging the thumb)
> does nothing. Closing and reopening the dialog brings it back, until it happens
> again.

## Why four green CI rounds did not see it

Every check the project had measured a **settled** dialog: solve the layout, then
look at the result. The static audits modelled the authored rectangles, the
portable suites exercised the pure solver, and the UI probe walked each tab after
a solve. All of that was green on a screen the user had photographed as
corrupted, because the failure is in the **order of operations**: one operation
leaves the dialog coherent, the next leaves it in a state nothing can recover
from, and every check that runs afterwards measures a *later* solve — or none.

## Root cause

`applySettingsDpiScale()` (the routine that re-scales the dialog when the monitor
scale changes) multiplied every child rectangle by the new scale and then dropped
the layout baseline (`g_settingsScroll.solved`) — and stopped there. The re-solve
was the caller's responsibility, and one caller never did it:

```
WM_DISPLAYCHANGE  →  the main window's handler  →  refreshSettingsDpi()
                  →  applySettingsDpiScale()  →  rescale children
                                              →  dropSettingsLayoutBaseline()
                                              →  (nothing else)
```

`WM_DISPLAYCHANGE` is a monitor topology / resolution / scale event: a
hot-plugged screen, an HDMI switch, a session reconnect, or a scale change that
arrives before the window's own `WM_DPICHANGED`. The dialog was then left with:

* **no layout baseline** — and `applySettingsScrollOffset()` begins with
  `if (solved.empty()) return;`, so every scroll path (wheel, arrows, thumb drag)
  updated the scrollbar and moved **nothing**;
* **a scroll state from the previous geometry** — offset, range, latch and the
  `WS_VSCROLL` style bit all survived the drop, so the scrollbar either claimed a
  page that no longer existed or was missing while the content overflowed;
* **children rescaled, window not refitted** — at a scale increase the page
  content lands below its own viewport, so the page area shows nothing;
* **regions from the old geometry** — a child that was region-clipped to an empty
  region (any child outside the viewport at the time) stays invisible, because
  nothing can clear a region without a baseline;
* **a poisoned next solve** — the solver reads the live rectangles as its input,
  so the next reflow bakes the un-normalized geometry in instead of undoing it.

The user sees exactly the report: a page with no content and a scrollbar that is
either dead or gone. Only closing and reopening the dialog recovers.

## The fix (UI/presentation only — no engine, hook, TSF or persistence change)

1. **`applySettingsDpiScale()` owns the whole transition**: rescale → drop the
   baseline → **re-solve**. A rescaled dialog that is not solved is now
   impossible to produce, whatever the caller.
2. **The settings window handles `WM_DISPLAYCHANGE` itself** (it is a top-level
   window and receives the broadcast directly) instead of depending on the main
   window's handler.
3. **`dropSettingsLayoutBaseline()` is total**: it clears the solver's rectangles,
   the viewport, the per-tab depths, the offset, the range, the latch **and the
   `WS_VSCROLL` bit**. A dialog with no layout may not claim a scroll state.
4. **One owner for the scrollbar, corrected in both directions**: the post-clamp
   decision is recomputed once from the clamped viewport and applied whether the
   bar has to appear (content unreachable — BS-01) or disappear (a page that fits
   paid 17 px of width for a thumb that cannot move), re-reading the client and
   keeping the tab control inside it.

## Evidence

**The harness (new CI capability).** `tools/ui_probe` gained the
operation-sequence half:

* a **named deterministic scenario**: a dialog read mid-scroll → a display change
  that rescales and drops the baseline → the app's own recovery operations
  (reflow, back to the top, tab switch);
* a **seeded fuzz pass** over the app's real paths — `WM_VSCROLL`, `WM_MOUSEWHEEL`,
  `WM_TIMER`, `TCN_SELCHANGE`, the DPI path, the display-change response, resize,
  text scale and the tick's growth request — 8 fixed seeds, a fixed step count
  (no wall clock, no runner influence), over the CI runner's own scale plus 120 %
  and 150 %, and text scales 100/125/150 %;
* **twelve invariants asserted after EVERY operation** on the app's own numbers
  (`KieeKeyProbeScrollState`: offset, range, latch, `WS_VSCROLL`, clamped
  viewport, per-tab solved depths, real `SCROLLINFO`, baseline presence) plus the
  live rectangles and window regions:

  | | invariant |
  |---|---|
  | I1 | offset 0 ⇒ at least one page control of the current tab is visible |
  | I2 | no visible page control starts above the page top |
  | I3 | the stored content depth matches the app's own deepest control |
  | I4 | `range == contentBottom − viewportBottom`, recomputed, never latched |
  | I5 | bar latch, `WS_VSCROLL` bit and Win32's own enable rule agree |
  | I6 | a control inside the viewport is never region-clipped to nothing |
  | I7 | at offset 0 a control fully inside the viewport carries no region |
  | I8 | a forced full repaint changes zero client pixels (real screen) |
  | I9 | a reflow never moves a control up/sideways, never shrinks the depth |
  | I10 | returning to offset 0 restores the settled visible set |
  | I11 | the page paints content, not background only (real screen) |
  | I12 | the app's own text measurement fits the control's box |

**RED on the pre-fix tree** (CI run `35863565160`, x64 job) — the harness forced
the display-change response to rescale the dialog while the app believed a
different scale, which is the state the user's machine reaches by itself:

```
harness 2880 steps / 2884 assertions / 2576 violations
[inv_I1=1724 inv_I3=368 inv_I4=368 inv_I5=17 inv_I11=1 inv_I12=98]
scenarios 2 run, 2 with violations
invariants (checks/violations) I1:1864/1724 I2:3932/0 I3:5768/368 I4:2884/368
                               I5:5768/17 I6:35621/0 I8:2/0 I9:228/0 I11:2/1 I12:2383/98
trace 500 lines (ui_probe_trace.jsonl)
```

next to the probe's own findings on the same run:

```
[empty_page] none of the 6 controls of this tab is visible (scroll travel 18289px):
             the whole tab starts below the page bottom (first control at y=18488,
             page 22,214 355x405) — the content has drifted out of the viewport
[scroll_pos] asked for offset 4572 (line steps), the app reports 1536 of a 18289 px travel
[outside_page] id 610 (Button) at 36,18488 744x420 is outside the reachable page
```

and `t0..t8 @100%` all clean while every tab at **150 %** is red — the feature is
scale-dependent, which is why a 100 % desktop can run it for months.

**GREEN after the fix**: the same harness on the fixed tree (this release's CI run)
reports `0` findings, `0` harness violations, with the step/assertion counts still
in `ui_probe.json` and in the CI digest line — a green run cannot mean "the
sequence checks never ran".

## What is still UNVERIFIED (Windows)

* The harness's fuzz op "display change" tells the app the monitor now reports
  150 % (the CI runner has one real DPI) and drives the app's own response. Which
  real-world event (hot-plug, HDMI switch, scale change, RDP reconnect, a window
  moved across monitors) leaves the app's scale belief stale on *your* machine is
  **UNVERIFIED (Windows)** — the fix removes the whole class (no caller can leave
  the dialog scaled-but-not-solved, and no baseline-less dialog keeps a scroll
  state), but only your machine can confirm the symptom is gone.
* The 120 %/144 %/192 % layout arithmetic beyond the CI runner's own scale is
  driven through the app's real DPI path, not by a real monitor at that scale.
* Whether a real 100 % desktop ever reaches the failing sequence is UNVERIFIED —
  the CI evidence is that the sequence is reachable and was reproduced.

## Test checklist

See `TESTING.txt` (Vietnamese) shipped next to the binary: it walks through the
digest match, the nine tabs, the scrollbar drag test, the diagnostic report
header/footer and what to send back if something is still wrong.
