# KieeKey v1.3.0-beta8fix2 — "chữ bị nhân bản": the ghosts of the previous frame

Windows file version **1.3.0.11** · UI string **1.3.0-beta8fix2** · built from
`main` after the merge of PR #35 (branch `arena/01a0d7f1-kieekey`) — the fix is
a new commit, not a re-tag.

> **Status: UNVERIFIED (Windows).** Nothing on the CI runner reproduces the
> defect — seven measurement rounds proved every CI-reachable state clean. The
> fix ships on mechanism; the acceptance that matters is YOUR photograph of a
> clean dialog on the machine that showed the breakage. Please follow
> `TESTING_v1.3.0-beta8fix2.txt`.

## What the tester reported (v1.3.0-beta8fix1, 150 % DPI, default window size)

* the old tab's content visible over the new one after clicking a tab —
  worst on tab **Cấp độ**, title/version text drawn twice ~150 px apart;
* content clipped at the right edge while the vertical scrollbar IS visible;
* **"kéo thì các chữ bị nhân bản"** — dragging the scrollbar duplicates text;
* machine: **Windows 10 LTSC 21H2** (build 19044), single monitor at 150 %,
  x64 build; the breakage reproduces deterministically across reopens.

## What seven measurement rounds proved (all on CI, all green)

The probe harness gained the open path itself and the pixel truth:

| round | axis measured | verdict |
|---|---|---|
| 1 | open path at default geometry + tray-open tabs (I13/I14/I15) | 0 violations |
| 2 | discriminants the runner lacked: landed ticks, growth reflows, real `WM_DPICHANGED`, bar flip-flop (E1–E4) | 0 violations |
| 3 | full-height geometry + width sweep with the bar moving (E5) | 0 violations |
| 4 | stale-pixel audit at the default-open geometry, tab by tab (E6) | 0 findings |
| 5 | classic vs. themed visual-style mode (E7) + the runner's own facts in the report | identical geometry, 0 findings |
| 6 | synthesized scroll round-trip with mid-scroll pixel audit (E8) | 0 findings |
| 7 | a REAL OS thumb drag via SendInput, pixels audited mid-drag and after the trip home (E9) | 0 findings |

Totals on the final tree: **4 320 harness steps / 5 196 assertions /
0 violations**, invariants I1–I15 + R1 (609 477 bar-latch checks among them),
**3 456 controls, 16 963 checks, 89 screen pixel audits, 0 findings**.
The report rides the runner's facts (`host: win 10.0.20348 appThemed 1
themeActive 1 dwm 1`) — every run is pinned to the machine that measured it.

Measurements worth keeping: the scrollbar's range **latches per tab at open**
(tab 8 latches 0 while tab 0 carries the range); scrolling only exists once
the dialog stands at its full content height; theme mode changes nothing the
harness can reach.

## The fix (BS-23d): the repaint becomes the whole-tree superset

The ghost class — old tab content over the new one, duplicated text while
scrolling — is a state where **some member of the window tree holds a frame
the dialog's own paint can never reach**: a moved, region-clipped child whose
own update region went stale; a band the non-client area owns.
`InvalidateRect(NULL, TRUE) + UpdateWindow` erased exactly the dialog's own
region and nothing else's — seven rounds said that was enough on the runner,
and the user's LTSC machine said it is not.

`settingsRepaintAll` is now:

```cpp
::RedrawWindow(hwnd, nullptr, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
               RDW_UPDATENOW | RDW_FRAME);
```

the exact primitive the probe's own pixel audit (`checkStalePixels`) uses to
force a clean frame. One call after every state change — tab switch, scroll
step, solve. No loop, no timer: a superset of the old pair by construction.
Pinned by paint rule 3 and a seed mutation in `tests/verify_audit_seeds.py`
(110 seeds caught).

## What did NOT change

No engine, hook, TSF or persistence behaviour. No new dependencies. The
layout model (`ok::layout`) is untouched — this release is presentation
layer only.
