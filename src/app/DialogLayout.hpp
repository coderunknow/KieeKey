//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: src/app/DialogLayout.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// KieeKey — DialogLayout.hpp (v1.3.0-beta3)
// Pure, portable layout maths for the Win32 settings dialog and the Chaos Lab.
//
// THE BUG CLASS THIS EXISTS TO KILL
//   Both surfaces were laid out with HAND-WRITTEN pixel rectangles sized for
//   one font at one DPI, in one language. A STATIC control word-wraps inside
//   its rectangle and silently CLIPS whatever does not fit vertically: the
//   text is not scrollable, not ellipsized and not reachable by any user
//   action — it is simply gone ("chữ bị mất, không có cách nào hiện ra được").
//   The same fixed rectangles are why nine tab headers were squeezed into a
//   536 px tab control: Windows shrinks the tabs to fit and clips the labels
//   mid-word, so the last tabs ("Phòng Chaos", "AI Rival", "Tiến trình") were
//   unreadable and their content effectively unreachable.
//
//   The fix is not "make the numbers bigger by hand" — the next string edit or
//   the next font/DPI/language would break them again. The fix is to MEASURE
//   at runtime and let this module solve the layout:
//
//     * planTabs()      — decide one-row/multi-row and full/short labels from
//                         the measured label widths and the available width.
//     * autoFit()       — grow every label to its measured height, push the
//                         controls below it down, grow the enclosing group box,
//                         and report how much extra dialog height is needed.
//     * findOverlaps()  — the audit that proves nothing is stacked on top of
//                         anything else (also used by the diagnostics panel so
//                         a user can report a real overlap instead of guessing).
//
//   Measurement itself stays in the Win32 layer (DrawTextW + DT_CALCRECT with
//   the REAL font on the REAL monitor), so no font metric is ever guessed
//   here: this module only consumes measured numbers. That is what makes it
//   portable AND what makes its unit tests meaningful — they exercise the
//   solver, not a font model.
//----------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ok::layout {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] int right()  const noexcept { return x + w; }
    [[nodiscard]] int bottom() const noexcept { return y + h; }
};

// True when two rectangles share any interior pixel. Touching edges (a label
// ending exactly where the next one starts) is NOT an overlap.
[[nodiscard]] inline bool overlaps(const Rect& a, const Rect& b) noexcept {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

// Containment used to discover "this label belongs to that group box" from the
// authored rectangles, without a hand-maintained parent table.
[[nodiscard]] inline bool contains(const Rect& outer, const Rect& inner) noexcept {
    return outer.x <= inner.x && outer.y <= inner.y &&
           inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

//---------------------------------------------------------------------------
// v1.3.0-beta8 (bug BS-10) — page content must start BELOW the tab strip.
//
// The nine tab labels wrap to a SECOND row when the dialog is narrow or the
// font is larger (TCS_MULTILINE, planned by ok::layout::planTabs). The display
// rectangle then starts one row lower — and the authored page rectangles were
// solved for a single-row strip, so the top of the page (group boxes at y=100,
// labels at y=110) ended up hidden UNDER the tab labels. The CA-03 probe found
// it on the CI runner: `page=[16,114,...]` with `id 555 at 24,100` etc.
//
// The rule is deliberately data-driven and idempotent: a page whose first
// control already starts at/below the display rectangle shifts by 0, so
// re-solving an already-solved dialog changes nothing.
//---------------------------------------------------------------------------
[[nodiscard]] inline int pageTopShiftPx(int authoredTopPx, int viewportTopPx) noexcept {
    return std::max(0, viewportTopPx - authoredTopPx);
}

//---------------------------------------------------------------------------
// v1.3.0-beta8 (bug BS-17) — THE SOLVER MUST START FROM THE UNSCOLLED LAYOUT.
//
// "Scrolling" in this dialog is a RENDER operation: it moves the page children
// up by the offset and clips them to the viewport (scrollChildRect below). It
// is not a layout change — but the solver took its input geometry from the live
// window rectangles, and `autoFit` may only ever move a control DOWN. So every
// re-solve that happened while the user was scrolled re-absorbed the scroll
// offset as a layout change:
//
//   * the row that scrolling had pushed ABOVE the page top was `pageTopShiftPx`'s
//     `authoredTop`, so the shift became `offset + (realGap)` and the whole page
//     was dragged back down by the offset it should not have known about;
//   * the plan then REPLACED the baseline, so the next reflow started from the
//     drifted geometry and drifted again — up to `offset` px per reflow.
//
// The timer asks for a reflow every time a live row needs more room, so on a
// 150 % desktop with a scrolled page this marched the whole page upward: rows
// left the top of the viewport, the reported content depth shrank with them, the
// scroll range collapsed to 0 and the page ended up empty with no scrollbar at
// all ("mất nội dung, không hiện scrollbar"), while every portable check and the
// CI probe — both of which measure a freshly solved, unscrolled dialog — stayed
// green.
//
// The rule: the solver's input for a page child is the BASELINE it produced last
// time (the layout, at offset 0) — never the live rectangle, which is the
// render. A child with no baseline yet (the first solve, before any scroll can
// exist) uses its live rectangle with the current offset added back; this
// function is that one case, shared by the app and by tests.
[[nodiscard]] inline Rect solverInputRect(const Rect& live, int scrollOffsetPx) noexcept {
    return Rect{live.x, live.y + scrollOffsetPx, live.w, live.h};
}

//---------------------------------------------------------------------------
// v1.3.0-beta8 (bug BS-09) — the bottom chrome row keeps its X when the
// dialog grows.
//
// The in-app ON/OFF toggle and the OK/Cancel/Apply row are authored BELOW the
// tab control's viewport, so they are not page content: the caller anchors them
// to the window refit by moving them DOWN with `clientDelta`. The beta7 apply
// loop handed SetWindowPos an X of 0 with SWP_NOSIZE but WITHOUT SWP_NOMOVE —
// on real Windows that moves every button in the row to the left edge and
// stacks the four of them on top of each other (and SetWindowPos applies X and
// Y whenever SWP_NOMOVE is absent; SWP_NOSIZE only suppresses cx/cy). Nothing
// caught it: scripts/audit_layout.py models rectangles, not SetWindowPos flags,
// and the defect only shows up when the refit actually grows the dialog — i.e.
// on a real monitor with a real font. The first CI run of tools/ui_probe (CA-03)
// found it: four buttons at x=0, three of them overlapping by 76x30/80x30 px.
//
// The decision is modelled here so it is unit-testable and cannot regress:
// a chrome rectangle at/below `tabBottom - slackPx` moves down by `clientDelta`
// and keeps its authored X; anything above that line (the header) does not move.
//---------------------------------------------------------------------------
struct BottomRowMove {
    bool moves = false;   // this chrome control sits in the bottom button row
    Rect rect{};          // where it must end up (equals the input when !moves)
};

[[nodiscard]] inline BottomRowMove bottomRowMove(const Rect& chrome, int tabBottom,
                                                int slackPx, int clientDelta) noexcept {
    BottomRowMove m;
    m.rect = chrome;
    if (chrome.y < tabBottom - slackPx) { return m; }
    m.moves = true;
    m.rect.y = chrome.y + clientDelta;
    m.rect.x = chrome.x;   // never sideways: the row slides down, not left
    return m;
}


//---------------------------------------------------------------------------
// One control as authored, plus what the runtime measurement said about it.
//---------------------------------------------------------------------------
struct ControlSpec {
    // Page/tab index sentinel for the header and the button row.
    static constexpr int kAlwaysVisible = -1;

    int id = 0;
    Rect rect;
    // Page/tab index; kAlwaysVisible for the header and the button row.
    int tab = kAlwaysVisible;
    // Measured text height in pixels (DrawTextW DT_CALCRECT). 0 means "not a
    // text control" or "measured smaller than the authored box".
    int requiredHeight = 0;
    // Only label-like controls may grow: a grown button/combobox/edit would
    // change its own semantics (a taller combo box is a taller drop-down).
    bool growable = false;
    // Group boxes never grow on their own; they are stretched to keep
    // containing their children.
    bool groupBox = false;
};

struct LayoutPlan {
    // Same order and size as the input: the rectangle each control should get.
    std::vector<Rect> rects;
    // Controls whose height was increased, and by how much in total.
    int grownControls = 0;
    int totalGrowthPx = 0;
    // Controls that STILL do not fit after the solver ran, because the page ran
    // out of room (the caller must enlarge the window or shorten the string).
    std::vector<int> clippedIds;
    // Extra pixels the page needs beyond `pageBottom` (0 = it fits).
    int extraHeightPx = 0;
    // The lowest bottom edge any control reaches after the solve.
    int contentBottom = 0;
};

// A group box keeps this much air below its last child (the authored dialogs use
// 8-16 px; the value only has to be >= the authored padding so the stretch
// never crops a child).
inline constexpr int kGroupBoxBottomPadPx = 10;

//---------------------------------------------------------------------------
// autoFit — grow labels to their measured height and reflow the page below
// them.
//
// Rules (all asserted in tests/test_dialog_layout.cpp):
//   * A control is only ever MOVED DOWN, never up or sideways: the authored
//     columns stay intact, so the dialog keeps its visual design.
//   * Everything on the same page whose top edge is at or below the grown
//     control's top edge shifts down by the same delta, preserving the
//     authored gaps.
//   * A group box is stretched to keep containing every child it contained
//     before the solve (children are discovered by authored containment).
//   * Always-visible controls (header, button row) participate in the shift
//     only when they sit below a grown control on the same page — the button
//     row is therefore anchored by the caller via `pageBottom` growth, i.e.
//     the caller moves the row by LayoutPlan::extraHeightPx.
//   * Nothing is ever shrunk below its authored size.
//---------------------------------------------------------------------------
[[nodiscard]] inline LayoutPlan autoFit(const std::vector<ControlSpec>& controls,
                                        int pageTop,
                                        int pageBottom) {
    LayoutPlan plan;
    plan.rects.reserve(controls.size());
    for (const ControlSpec& c : controls) { plan.rects.push_back(c.rect); }
    if (controls.empty()) { return plan; }

    // Group-box parent lookup: for every control, the innermost authored group
    // box on the same page that contains it.
    std::vector<int> parent(controls.size(), -1);
    for (std::size_t child = 0; child < controls.size(); ++child) {
        const ControlSpec& c = controls[child];
        if (c.groupBox) { continue; }
        int best = -1;
        int bestArea = 0;
        for (std::size_t g = 0; g < controls.size(); ++g) {
            if (!controls[g].groupBox || controls[g].tab != c.tab) { continue; }
            if (!contains(controls[g].rect, c.rect)) { continue; }
            const int area = controls[g].rect.w * controls[g].rect.h;
            if (best < 0 || area < bestArea) { best = static_cast<int>(g); bestArea = area; }
        }
        parent[child] = best;
    }

    // Growth per control, in authored order.
    std::vector<int> growth(controls.size(), 0);
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const ControlSpec& c = controls[i];
        if (!c.growable || c.groupBox) { continue; }
        const int needed = c.requiredHeight;
        if (needed > c.rect.h) {
            growth[i] = needed - c.rect.h;
            plan.rects[i].h = needed;
            ++plan.grownControls;
            plan.totalGrowthPx += growth[i];
        }
    }

    // Shift: a control moves down by the total growth of every control on the
    // same page that is ABOVE it (top edge strictly greater). Group-box
    // children use their own growth; the group box itself is stretched after
    // the shift so it still contains them.
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const ControlSpec& c = controls[i];
        if (c.groupBox) { continue; }
        int shift = 0;
        for (std::size_t j = 0; j < controls.size(); ++j) {
            if (j == i || growth[j] == 0) { continue; }
            if (controls[j].tab != c.tab) { continue; }
            if (controls[j].rect.y <= c.rect.y) { shift += growth[j]; }
        }
        plan.rects[i].y += shift;
    }
    for (std::size_t i = 0; i < controls.size(); ++i) {
        if (!controls[i].groupBox) { continue; }
        int shift = 0;
        for (std::size_t j = 0; j < controls.size(); ++j) {
            if (growth[j] == 0 || controls[j].groupBox) { continue; }
            if (controls[j].tab != controls[i].tab) { continue; }
            if (controls[j].rect.y <= controls[i].rect.y) { shift += growth[j]; }
        }
        plan.rects[i].y += shift;
    }

    // Stretch group boxes to contain their (moved, grown) children.
    for (std::size_t g = 0; g < controls.size(); ++g) {
        if (!controls[g].groupBox) { continue; }
        int bottom = plan.rects[g].bottom();
        for (std::size_t i = 0; i < controls.size(); ++i) {
            if (parent[i] != static_cast<int>(g)) { continue; }
            bottom = std::max(bottom, plan.rects[i].bottom() + kGroupBoxBottomPadPx);
        }
        plan.rects[g].h = bottom - plan.rects[g].y;
    }

    // Content bounds + the clip verdict.
    //
    // v1.3.0-beta5 (bug B1): the page bottom is measured over the PAGE
    // controls ONLY. The always-visible chrome (header ABOVE the viewport,
    // button row BELOW it) is not page content: the button row is authored
    // below `pageBottom` by design, so including it made extraHeightPx
    // permanently non-zero (~42 px at 96 dpi) — the window grew on EVERY
    // open even when nothing was clipped, and on small work areas the
    // all-or-nothing growth application then skipped the growth ENTIRELY
    // while the page children had already moved to their solved rects:
    // guaranteed overlap + clipping. The caller anchors the button row via
    // refitWindow() below, which is where the chrome now participates.
    plan.contentBottom = pageTop;
    for (std::size_t i = 0; i < controls.size(); ++i) {
        if (controls[i].tab == ControlSpec::kAlwaysVisible) { continue; }
        plan.contentBottom = std::max(plan.contentBottom, plan.rects[i].bottom());
    }
    if (plan.contentBottom > pageBottom) { plan.extraHeightPx = plan.contentBottom - pageBottom; }

    // A control is "still clipped" when its own measured text does not fit the
    // rectangle it ended up with (only possible for non-growable text controls,
    // e.g. a label inside a fixed-height row, or when the page ran out).
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const ControlSpec& c = controls[i];
        if (c.requiredHeight <= 0 || c.groupBox) { continue; }
        if (c.requiredHeight > plan.rects[i].h) { plan.clippedIds.push_back(c.id); }
    }
    return plan;
}

//---------------------------------------------------------------------------
// Overlap audit — pairs of controls on the SAME page whose rectangles share a
// pixel. Group boxes are excluded as "outer" candidates because containing
// children is their purpose; two group boxes overlapping each other IS
// reported.
//---------------------------------------------------------------------------
struct OverlapPair {
    int a = 0;
    int b = 0;
    int pixels = 0;   // shared area, for triage ("1 px touch" vs "half a label")
};

[[nodiscard]] inline std::vector<OverlapPair> findOverlaps(
        const std::vector<ControlSpec>& controls,
        const std::vector<Rect>* solved = nullptr) {
    std::vector<OverlapPair> pairs;
    const std::size_t n = controls.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Rect& a = (solved != nullptr) ? (*solved)[i] : controls[i].rect;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (controls[i].tab != controls[j].tab) { continue; }
            if (controls[i].groupBox && !controls[j].groupBox) { continue; }
            if (controls[j].groupBox && !controls[i].groupBox) { continue; }
            const Rect& b = (solved != nullptr) ? (*solved)[j] : controls[j].rect;
            if (!overlaps(a, b)) { continue; }
            const int ix = std::min(a.right(), b.right()) - std::max(a.x, b.x);
            const int iy = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
            pairs.push_back(OverlapPair{controls[i].id, controls[j].id,
                                        std::max(0, ix) * std::max(0, iy)});
        }
    }
    return pairs;
}

//---------------------------------------------------------------------------
// Tab header planning.
//
// A single-row Win32 tab control does NOT scroll: when the items do not fit it
// shrinks every tab to availableWidth/itemCount and clips the label. Nine
// Vietnamese labels in 536 px is exactly that case. The solver therefore has
// three honest options, tried in order:
//
//   1. one row, full labels   (fits — the design intent)
//   2. one row, short labels  (fits — the header stays one line tall)
//   3. multi-row, short labels (always fits given enough rows; costs height)
//
// The caller must create the tab control with TCS_MULTILINE when rows > 1 and
// give it rows * rowHeight extra pixels.
//---------------------------------------------------------------------------
struct TabPlan {
    int rows = 1;
    bool multiline = false;
    bool useShortLabels = false;
    int itemWidth = 0;        // the widest single item (full or short as chosen)
    int requiredWidth = 0;    // total width the chosen labels need, one row
    int availableWidth = 0;
    int controlHeight = 0;    // rows * rowHeight + display inset
    bool fits = true;         // false only if even multi-row cannot fit (never,
                              // given rows are computed by ceil division)
};

[[nodiscard]] inline TabPlan planTabs(const std::vector<int>& labelWidths,
                                      const std::vector<int>& shortLabelWidths,
                                      int availableWidth,
                                      int paddingPerItem,
                                      int rowHeight,
                                      int displayInset,
                                      int maxRows = 3) {
    TabPlan plan;
    plan.availableWidth = availableWidth;
    const std::size_t count = labelWidths.empty() ? 0 : labelWidths.size();
    if (count == 0 || availableWidth <= 0 || rowHeight <= 0) {
        plan.controlHeight = displayInset;
        return plan;
    }
    const auto total = [&count, paddingPerItem](const std::vector<int>& widths) {
        int sum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const int w = (i < widths.size()) ? widths[i] : 0;
            sum += w + paddingPerItem;
        }
        return sum;
    };
    const auto widest = [&count](const std::vector<int>& widths) {
        int max = 0;
        for (std::size_t i = 0; i < count && i < widths.size(); ++i) {
            max = std::max(max, widths[i]);
        }
        return max;
    };

    const int fullTotal = total(labelWidths);
    const bool shortProvided = shortLabelWidths.size() == count;
    const int shortTotal = shortProvided ? total(shortLabelWidths) : fullTotal;

    if (fullTotal <= availableWidth) {
        plan.rows = 1;
        plan.useShortLabels = false;
        plan.requiredWidth = fullTotal;
        plan.itemWidth = widest(labelWidths);
    } else if (shortProvided && shortTotal <= availableWidth) {
        plan.rows = 1;
        plan.useShortLabels = true;
        plan.requiredWidth = shortTotal;
        plan.itemWidth = widest(shortLabelWidths);
    } else {
        const int perRow = shortProvided ? shortTotal : fullTotal;
        int rows = (perRow + availableWidth - 1) / availableWidth;
        if (rows < 1) { rows = 1; }
        if (maxRows > 0 && rows > maxRows) { rows = maxRows; }
        plan.rows = rows;
        plan.multiline = rows > 1;
        plan.useShortLabels = shortProvided;
        plan.requiredWidth = perRow;
        plan.itemWidth = widest(shortLabelWidths.empty() ? labelWidths : shortLabelWidths);
    }
    plan.controlHeight = displayInset + plan.rows * rowHeight;
    return plan;
}

//---------------------------------------------------------------------------
// Window growth helper: the caller knows the monitor work area. Returns the
// new client height (never above workAreaHeight) and how much of the request
// could not be satisfied (which autoFit then reports as clipped controls).
//---------------------------------------------------------------------------
struct WindowGrowth {
    int newClientHeight = 0;
    int unsatisfiedPx = 0;
};

[[nodiscard]] inline WindowGrowth growWindow(int currentClientHeight,
                                             int requestedExtraPx,
                                             int maxClientHeight) noexcept {
    WindowGrowth result;
    if (requestedExtraPx <= 0) {
        result.newClientHeight = currentClientHeight;
        return result;
    }
    const int wanted = currentClientHeight + requestedExtraPx;
    if (maxClientHeight > 0 && wanted > maxClientHeight) {
        result.newClientHeight = std::max(currentClientHeight, maxClientHeight);
        result.unsatisfiedPx = wanted - result.newClientHeight;
        return result;
    }
    result.newClientHeight = wanted;
    return result;
}

//---------------------------------------------------------------------------
// v1.3.0-beta5 (bug B1) — window refit model.
//
// growWindow() answers "how tall would the window like to be"; refitWindow()
// is the DECISION the caller must apply atomically: grow (or shrink) toward
// that wish, clamp to the monitor work area, keep the window fully on
// screen, and report what is left over as a SCROLL RANGE instead of silently
// skipping the whole adjustment (the beta4 all-or-nothing bug: when the
// growth did not fit, NOTHING was applied — the page children had already
// moved to their solved rects, so they overlapped the unmoved button row and
// clipped at the old window bottom, with no way to scroll to them).
//
// Coordinates: `window` is the FULL window rect in screen pixels (borders and
// caption included); `clientHeightPx` is its current client height; the
// viewport/content pair is in CLIENT pixels (the page viewport bottom edge
// and the solved page content bottom edge). `workArea` is the monitor work
// area in screen pixels. The model is pure — the Win32 layer applies it.
//---------------------------------------------------------------------------

// Move/resize `r` so it is fully inside `work`, shrinking it only when it is
// larger than the work area itself. Prefers the minimal movement: a window
// already on screen does not jump.
[[nodiscard]] inline Rect fitRectToWorkArea(const Rect& r, const Rect& work) noexcept {
    Rect out = r;
    if (work.w <= 0 || work.h <= 0) { return out; }
    if (out.w > work.w) { out.w = work.w; }
    if (out.h > work.h) { out.h = work.h; }
    if (out.x < work.x) { out.x = work.x; }
    if (out.right() > work.right()) { out.x = work.right() - out.w; }
    if (out.y < work.y) { out.y = work.y; }
    if (out.bottom() > work.bottom()) { out.y = work.bottom() - out.h; }
    return out;
}

struct WindowRefit {
    Rect  windowRect   {};   // new full-window rect (fitted into the work area)
    int   clientDelta  = 0;  // client height change: grow the tab + shift the
                             // bottom chrome row by exactly this (may be < 0
                             // when the authored window exceeded the work area)
    int   scrollRange  = 0;  // page content px below the new viewport bottom
    bool  scrollNeeded = false;
};

[[nodiscard]] inline WindowRefit refitWindow(const Rect& window,
                                             int clientHeightPx,
                                             int viewportBottomPx,
                                             int contentBottomPx,
                                             const Rect& workArea) noexcept {
    WindowRefit out;
    const int chromePx = std::max(0, window.h - clientHeightPx);
    // 1. The client the CONTENT wants: grown to the solved page bottom.
    int wantedClient = clientHeightPx;
    if (contentBottomPx > viewportBottomPx) {
        wantedClient = clientHeightPx + (contentBottomPx - viewportBottomPx);
    }
    // 2. Clamp to what the work area can show (shrink included: an authored
    //    window taller than a small screen must still be fully reachable).
    int maxClient = workArea.h > 0 ? workArea.h - chromePx : wantedClient;
    if (maxClient < 120) { maxClient = 120; }   // never collapse to nothing
    const int newClient = std::min(wantedClient, maxClient);
    out.clientDelta = newClient - clientHeightPx;
    // 3. Whatever the viewport still cannot show becomes scroll range — the
    //    content is reachable instead of clipped (WS_VSCROLL fallback).
    const int newViewportBottom = viewportBottomPx + out.clientDelta;
    out.scrollRange = std::max(0, contentBottomPx - newViewportBottom);
    out.scrollNeeded = out.scrollRange > 0;
    // 4. New window rect, fully inside the work area (minimal movement).
    out.windowRect = fitRectToWorkArea(
        Rect{window.x, window.y, window.w, chromePx + newClient}, workArea);
    return out;
}

//---------------------------------------------------------------------------
// v1.3.0-beta5 (bug B1) — scroll fallback model for the settings dialog.
//
// The dialog's page children are direct children of the dialog (GetDlgItem
// must keep working), so "scrolling" = moving the solved page rects up by the
// scroll offset and CLIPPING them to the tab viewport. A child entirely
// outside the viewport gets an empty region (hidden without SW_HIDE, so the
// showTab() visibility contract is untouched); a child straddling an edge is
// clipped to it. All maths here; the Win32 layer only applies rects+regions.
//---------------------------------------------------------------------------
struct ScrolledChild {
    bool     visible  = true;   // false => fully outside the viewport
    Rect     rect     {};       // new position (solved rect shifted by -offset)
    bool     clipped  = false;  // true => a clip region must be applied
    Rect     clip     {};       // clip rect in CHILD-LOCAL coordinates
};

[[nodiscard]] inline ScrolledChild scrollChildRect(const Rect& solved,
                                                   int offset,
                                                   const Rect& viewport) noexcept {
    ScrolledChild out;
    out.rect = Rect{solved.x, solved.y - offset, solved.w, solved.h};
    const Rect& r = out.rect;
    if (r.bottom() <= viewport.y || r.y >= viewport.bottom() ||
        r.right() <= viewport.x || r.x >= viewport.right()) {
        out.visible = false;
        out.clipped = true;
        out.clip = Rect{0, 0, 0, 0};
        return out;
    }
    Rect c = r;
    if (c.x < viewport.x) { c.w -= viewport.x - c.x; c.x = viewport.x; }
    if (c.y < viewport.y) { c.h -= viewport.y - c.y; c.y = viewport.y; }
    if (c.right() > viewport.right())   { c.w = viewport.right() - c.x; }
    if (c.bottom() > viewport.bottom()) { c.h = viewport.bottom() - c.y; }
    out.clipped = (c.x != r.x || c.y != r.y || c.w != r.w || c.h != r.h);
    out.clip = Rect{c.x - r.x, c.y - r.y, c.w, c.h};   // child-local
    return out;
}

// v1.3.0-beta8 (bug BS-12) — a runtime text change is a LAYOUT change.
//
// The 500 ms timer writes four rows with live text (diagnostics verdict, arcade
// status, AI stats, coaching advice) whose height the solver could not know
// when it solved the page. The beta8 build grew those rows ON THE SPOT: the
// neighbours below stayed where they were (the row ran under them) and the
// solver's baseline kept the OLD height, so the next scroll step resized the
// row back down — the line the user had just read was cut in half again
// (measured by the CI probe: 188px -> 168px at offset 7 on tab 6 @150%).
//
// The rule this models: if the text needs more room than the row has, the
// DIALOG must be re-solved (grown row + everything below shifted + window
// refitted + scroll range recomputed), never the row alone. 2 px of tolerance
// so a rounding difference never starts a reflow loop.
[[nodiscard]] inline bool rowNeedsReflow(int currentH, int neededH) noexcept {
    return neededH > currentH + 2;
}

// v1.3.0-beta8 (bug BS-16d) -- ASK ONCE PER REQUIREMENT, NOT ONCE PER TEXT.
//
// The timer asks for a reflow when a live row's text outgrew its box. The first
// version remembered the TEXT HASH, so a row whose text changes every tick (a
// counter, a hook latency, "đang gõ"/"nhàn rỗi") but whose REQUIRED HEIGHT does
// not re-solved the whole dialog -- all 121 children moved, the window refitted,
// the scroll range recomputed -- at 2 Hz, forever. The measured height is the
// only thing the solver acts on, so it is the only thing worth deduping: the
// caller asks again only when the row needs MORE room than the height it last
// asked for (plus the same 2 px the fit test tolerates). A row that can no
// longer be satisfied (window at the work-area clamp, growth applied as far as
// it goes) stops asking instead of looping.
[[nodiscard]] inline bool shouldRequestReflow(int lastRequestedNeedPx,
                                              int neededPx) noexcept {
    return neededPx > lastRequestedNeedPx + 2;
}

// v1.3.0-beta8 (bug BS-10) — THE WINDOW IS NOT ALWAYS AS TALL AS THE CONTENT.
//
// 150 % on a 1080p work area is the NORMAL case, not an edge: the refit grows
// the client by what the work area allows and the rest is supposed to become
// scroll. The beta8 arithmetic grew the TAB CONTROL by the intended client
// delta anyway, so at 150 % its display rectangle — and every page child inside
// it — ended far below the window, while the bottom chrome row, moved by the
// same delta, landed off-screen with it: the CI probe measured the button row
// at y=911 inside a 689-pixel window, i.e. OK / Huỷ / Áp dụng were not on the
// screen at all and the page could not scroll (the "viewport" claimed the
// content fit). Two rules, both pure:
//
//   * the tab control is never taller than the space left above the chrome row
//     INSIDE the client;
//   * the chrome row sits just below the tab display rectangle, but never below
//     the client.
[[nodiscard]] inline int tabHeightForClient(int tabTopPx, int clientBottomPx,
                                           int chromeBandPx) noexcept {
    const int available = clientBottomPx - tabTopPx - chromeBandPx;
    return (available > 0) ? available : 0;   // 0 => the caller keeps its minimum
}

[[nodiscard]] inline int chromeRowTopInClient(int belowTabTopPx, int clientBottomPx,
                                             int rowHeightPx) noexcept {
    const int maxTop = clientBottomPx - rowHeightPx;
    return (belowTabTopPx < maxTop) ? belowTabTopPx : maxTop;
}

// Standard scrollbar metrics for the fallback.
//
// v1.3.0-beta8 (bug BS-01) — THE OLD MODEL MADE THE BAR UNREACHABLE.
// It shipped as `nMax = overflow - 1` with `nPage = 90 % of the viewport`
// (beta5..beta7). Win32 DISABLES a scrollbar whenever `nPage >= nMax + 1`, so
// with a ~470 px viewport (nPage ~423) and the tens-of-pixels overflows this
// dialog actually produces (one wrapped label = +17 px) the bar was dead in
// exactly the cases it exists for — while `applySettingsScrollOffset()` had
// already region-clipped every child below the viewport. The user's text was
// invisible AND unreachable ("chữ bị che, không thể kéo").
//
// Correct model, used by src/app/main.cpp verbatim:
//   * 1 scroll unit == 1 px, so SCROLLINFO::nMax + 1 == the whole content;
//   * nPage == the viewport height (one page), never a fraction of it;
//   * the bar is therefore enabled iff `maxTravelPx > 0`;
//   * the thumb fraction is viewport / content — what every native scrollbar
//     shows — and the drag travel is exactly the overflow, so the last pixel
//     row of the page is reachable.
struct ScrollMetrics {
    int  contentPx   = 0;      // total content height == nMax + 1
    int  pagePx      = 0;      // one page == the viewport height
    int  linePx      = 16;     // arrow-key / wheel step
    int  maxTravelPx = 0;      // largest legal scroll offset == the overflow
    int  rangeMaxPx  = 0;      // SCROLLINFO::nMax (contentPx - 1, inclusive)
    bool enabled     = false;  // nPage < nMax + 1  <=>  maxTravelPx > 0

    [[nodiscard]] double thumbFraction() const noexcept {
        return contentPx > 0
                   ? static_cast<double>(pagePx) / static_cast<double>(contentPx)
                   : 1.0;
    }
};

[[nodiscard]] inline ScrollMetrics scrollMetrics(int viewportHeightPx,
                                                 int scrollRangePx) noexcept {
    ScrollMetrics m;
    m.pagePx      = std::max(1, viewportHeightPx);   // never 0: Win32 would
                                                     // drop the page info and
                                                     // hide the thumb entirely
    m.maxTravelPx = std::max(0, scrollRangePx);
    m.contentPx   = m.pagePx + m.maxTravelPx;
    m.rangeMaxPx  = m.contentPx - 1;
    m.enabled     = m.maxTravelPx > 0;
    return m;
}

} // namespace ok::layout
