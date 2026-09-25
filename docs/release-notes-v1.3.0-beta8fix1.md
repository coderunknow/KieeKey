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

## The second root cause: the tab control sat ABOVE the page (z-order)

The reported screen — a settings page showing background where its rows should be —
had a second, independent cause, and the harness had to be taught to look at the
**z-order** to see it. The tab control is created first and owns the whole page
rectangle; the ~120 page controls are created after it, so they live *above* it.
That order is not permanent: `showTab()` shows the active tab's controls with
`SW_SHOW` on every tab switch, and a window brought to the top of the sibling order
takes its whole rectangle with it. When the tab control ends up **above** a page
control, that control is still there — same rectangle, `WS_VISIBLE`, no region,
parented to the dialog — and is simply **not drawn**. Every window-state check the
project had was green while the user's screen showed an empty row.

The x64 probe run `35988174121` measured exactly that state:

```
scenario_screen_paint tab 3 client 543x689 app dpi 96 page 16,114 511x529
  strip 4/4 rows 1 tab 12,66 519x581 visible yes region set rows 2
  ctl: 522 sampled 28,114 250x18 live 28,114 250x18 rgn none vis y parent dlg painted 0/3
       514 sampled 290,114 210x18 live 290,114 210x18 rgn none vis y parent dlg painted 0/3
       ...
[I11] the page paints background only: 8 visible controls, none of their middle
      rows differs from the page background
```

Eight controls of the shown tab, at their own live rectangles, visible, unregioned —
and not one pixel of them on the screen, while the chrome (outside the tab's
rectangle) painted. The fix keeps the page children above the tab control by
construction: after the solve has applied every rectangle, the tab control is put at
the **bottom** of the dialog's children (`SetWindowPos(…, HWND_BOTTOM, …,
SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)` — a z-order change only), and the pixels
the change reveals are repainted synchronously (the parent's own invalidate cannot
cover them: a parent never paints its children's pixels — the probe's I8 measured
the stale frame at `3540 client pixels` before that repaint existed). The probe now
records each judged control's position in that order (`z N tabAbove|tabBelow`), so a
regression names its side of the stack instead of costing a run of guessing.

## The rest of the round (same symptom, other paths — and the measurements that hid them)

| code | root cause | invariant |
|---|---|---|
| BS-22 | the solver read the **live** (scrolled/rescaled) rectangles instead of the authored geometry | I5, I12 |
| BS-22c | the clamp may only narrow; the region follows the window; one owner for the latch/`WS_VSCROLL` pair | I5 |
| BS-22d/23 | the strip re-lays out in **both** directions; a row fits its own text in width | I1, I12 |
| BS-22e | the bar belongs to the window it serves; a text scale must be undoable | I5 |
| BS-22f | the font factory mints the face it was asked for; the scale knows every face | I1 |
| BS-22g/h/i | a measurement must prove its **precondition** (client calibration for the strip cycle; screen-capture provenance; unavailable passes report themselves) | I1, I11 |
| BS-22j/k | a label that does not fit **wraps** instead of being clipped; the capture proves whose frame it is | I6, I12 |
| BS-22l/o/p/q | the plan describes the **window that exists** in both directions (a combo's real height; the row count the control built) | I6, I1 |
| BS-22r | the render cross-check counted an unpainted DIB as "ink" | I11 (measurement) |
| BS-22t | the row count is read in **either** direction; the revealed page is repainted synchronously | I1, I8 |
| BS-22u | the strip cycle reads the state after it settles and reports the plan's own arithmetic | I1 (measurement) |
| BS-22v | the nine tab labels are measured with the font the **control** draws them with (`WM_GETFONT`), not `uiFont()` | **I1 (last)** |
| BS-22w | the text scale maps every face the **app minted, at any dpi**, by its **recorded** role; the restore is audited before the capture runs; the ink samples sit strictly inside each control | I11 (measurement) |

BS-22v is the link that closed the last red state. `solveSettingsLayout()` was the one
measurement in the solve that selected `uiFont()` instead of the control's own font
(the other two helpers, `measureSingleLineWidthPx()` and `measureStaticTextHeightPx()`,
already select `WM_GETFONT`). When anything puts a different face on the tab control
— the harness's text-scale path does, through the app's own font factory — the plan
measures 100 % labels while the control draws 1.5x ones: it answers `one row` for a
strip the control has already wrapped, `stripRows` and the style bit describe a strip
that is not on the screen, and the grow/shrink transition cannot be planned at all.
The probe's note from run `68544ea` carries the numbers that named it:
`a1:700/683 plan1(559/643) ctl1 ml0` — the plan and the control agree at a wide
client, and only the *font* was wrong.

### BS-22w — the flake the tag run exposed (after the merge)

The merge commit `55414e9` produced **two CI runs on the very same tree with
different verdicts**: run `35995109042` (push of `main`) passed all four jobs,
and run `35995128589` (push of the tag `v1.3.0-beta8fix1`) failed only `x64`,
with one violation — `[I11] the page paints background only`, `client 974x689
app dpi 96`, `rowH 37`, `itemTops 2,2,…` (the 150 % strip's metrics) on a pass
that believed it was at 96 dpi. Same tree, different verdict ⇒ the defect was
in a **measurement**, and it followed the timing, not the tree. Two defects:

1. **The text-scale mapping table did not know the app's own faces minted at
   another dpi.** `KieeKeyProbeFontScale()` mapped the app's faces at the
   *current* dpi plus every face the probe itself had applied. A face the app's
   rescale path (`rescaleChild`, through `applySettingsDpiScale`) minted at
   144 dpi is in neither set — so when the harness's scale returned to 100 %,
   controls wearing that face kept it. The plan measured labels with the bigger
   face, asked for a wider window, the solver widened the window to 974 px, and
   the capture judged a hybrid dialog **no pass ever built**. Which pass leaves
   which face behind depends on how the previous pass's rescale interleaved
   with the scale cycle — hence green on one run of the tree, red on another.
2. **The "has ink" measurement sampled three x-points on one row.** On a wide
   control whose label sits at the left edge, all three quarter-points land in
   the empty space after the text, so a *painted* control read `painted 0/3` —
   a false alarm that turned (1) into a blank-page finding.

The fix (measurement only — the product was never wrong): every font writer
reports its face into a registry **with the role it just classified**
(`kieeKeyProbeRememberAppFont`; the scale maps an app-minted face by its
recorded role, never by a height guess — at 150 % the body face is 19 px and
the title 30 px, at 144 dpi the body face is 20 px); `KieeKeyProbeFontScale()`
audits its own restore (`KieeKeyProbeUnmappedFontCount()`), and the harness
fails the I11 **pre-condition** (`scenario_screen_paint_fonts`) *before* the
capture whenever a control still wears another scale's face; the ink samples
are a 4×3 grid strictly inside each control. New paint-gate rule 12 and five
new seeds (`verify_audit_seeds.py`: 84 checks, 0 misses) pin all of it.

### BS-22w follow-up — the reflow invariant judges CONVERGENCE (CI of this PR)

The patch's CI rounds surfaced one more measurement defect one layer out, in
the fuzz's reflow invariant (I9). The evidence chain, all from annotations:

* `36012001327`: `[I9] the reflow moved id 611 up/sideways (66,224 330x33 ->
  66,203 330x33)` at step 31 seed 3 (tab 8, 100 %, dpi 144) — the BS-22w
  signature itself was already gone (`I11:6/0`).
* `36016664252`: the same move with the strip's own shape on both sides —
  identical (`rows 2 rowH 26 dispTop 155 fontPx 21`), the app's mirror
  identical (`shift96 3`, `offset 0`) — no reshape, no bar flip *inside* the
  reflow.
* `36018651106`: the convergence pass fired: a second identical re-solve moved
  id 610 (`344x420 -> 327x420`) — **17 px, the scrollbar's width at 144 dpi**:
  the solve's settle cascade (refit → bar latch → client width) crosses
  operation boundaries, and an earlier op can leave the live layout one settle
  behind the plan. The old direction clauses punished the reflow that
  *corrected* that stale geometry.

I9 is therefore now: **two identical re-solves in a row must agree** (control
list, rectangles to a pixel of placement tolerance, page depth). An unstable
or oscillating solve fails by definition; the settle cascade itself fails the
moment it needs more passes than the reflow gets (the BS-14/BS-15 app-bug
shape, made directly visible); the correction of a state an earlier op left
stale is traced (`reflow_settle_move` with the previous solve's rectangle) and
its soundness stays owned by I6 (live == baseline) and I12 (box holds the
solver's own measurement — the BS-12 growth-loss class), which assert on the
same post-state every step. Paint-gate rule 12 gained the two needles
(`reflowStripShape`, the convergence clause); the seed harness grew to
**86 checks, 0 misses**. App code untouched by the follow-up *so far* —
see part 2.

### BS-22w follow-up, part 2 — the 17 px the bar took (BS-22x, app fix)

Two more CI rounds turned the follow-up from a harness story into an app
bug with exact numbers. `36020387531` (@`a4096c1`) compacted the finding
ground so the annotation survived, and it named the state: strip identical
on all three reads (`rows 2 rowH 26 dispTop 181 fontPx 21`), plan
`3/715/321@381` on both sides, app shift unchanged — the solver was a
fixed point of its own arithmetic; the world it planned against was not.
A wrong first reading — "the tab header reserves three rows while the
items sit in two" — produced a display-rectangle reconciliation patch
(@`926c96f`, reverted here): the probe's row count was the liar. It
compared item 0's top with item N−1's and answered a binary *1 or 2*,
saturating at the second row — the control honestly had three rows
(dispTop 181 = 99 + 3 × 26 + pad, exactly consistent), and the harness
said so from `36022345344`'s ground on. (The probe's strip reader counts
every item top now, bucketed at half a row height.)

The real defect, BS-22x: **every page row is clamped at
`client.right − S(12)` measured before the scrollbar latch is decided.**
The latch can flip *inside the same solve* ("solve-planned", and BS-18's
"solve-final"); the flip re-fits the tab control to the bar-on client —
but when that re-fit lands on a width the tab control already had (an
earlier solve had sized it for a bar-on client, and the bar has since
hidden under it — hiding only widens the client and never moves the tab
control), BS-14's tab-width re-solve trigger sees no change. The rows
stay clamped at the bar-off bound, 17 px wider than the window they live
in; the next identical solve, planning with the bar on, clamps them 17 px
narrower — `id 610 (36,181 344x420 -> 36,181 327x420)`: call one clamped
at the bar-off client (398 − 18 − 36 = 344), call two at the bar-on
client (381 − 18 − 36 = 327). A solve must be a fixed point of itself;
this one was not.

The fix is the third trigger in the same bounded pass-0 guard family: at
the end of a solve, recompute the client bound; if the bar moved it after
the rows were measured, one extra bounded solve re-plans against the
width the window actually keeps. Presentation layer only — no engine,
hook, TSF, persistence or invariant change.

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

**GREEN after the fix**: the same harness on the final tree (CI run `35993697384`,
commit `e036e85`, all four jobs green — `Native regression (Linux)`, `x64`, `ARM64`,
`ARM64EC`) reports `0` findings and `0` harness violations, with the step/assertion
counts still in `ui_probe.json` and in the CI digest line — a green run cannot mean
"the sequence checks never ran":

```
harness 4320 steps / 4734 assertions / 0 violations []
scenarios 6 run, 0 with violations
invariants (checks/violations) I1:3086/0 I2:33415/0 I3:9042/0 I4:4521/0
          I5:9042/0 I6:108473/0 I8:3/0 I9:342/0 I11:3/0 I12:27590/0 R1:6/0
UI probe: 3456 controls, 14550 checks, 0 findings []
          (native DPI 96, scales 100/125/150, screen checks 27 run / 0 unavailable)
x64 CTest: tests=8; failures=0; disabled=0; skipped=0
```

The loop that got there, kept in `docs/FINAL_CHECK_v1.3.0-beta8fix1.md`:

```
1379 (77e8fea) → 56 → 84 → 309 → 383 → 335 → 30 → 4 → 4 → 2 → 1 → 0 (e036e85)
```

## Sau khi tag: lượt CI của chính tag (BS-22w)

The tag `v1.3.0-beta8fix1` first pointed at the merge commit `55414e9`. Its CI
run (`35995128589`) hit the BS-22w measurement flake above: job `x64` failed on
the one `[I11]` violation, and `Publish release` — the only job that creates the
GitHub Release — was `skipped`. **That run never released anything**, so the tag
was moved (deleted on the remote, re-created as an annotated tag) to the first
commit that carries BS-22w and whose tag CI run is green in all four jobs,
including `Publish release`. Nothing was withdrawn or rewritten: a release that
was never created cannot be retracted. The release notes, the final check and
`TESTING_v1.3.0-beta8fix1.txt` that ship with the release describe the BS-22w
tree, not the `55414e9` one.

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
