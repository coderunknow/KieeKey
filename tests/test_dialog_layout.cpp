//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: tests/test_dialog_layout.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// DialogLayout solver — PORTABLE proof for bug #1 (v1.3.0-beta3).
//
// User report: "chữ bị mất do bị đè, không có cách nào hiện ra được" in the
// settings dialog, plus the Chaos Lab font making text unreadable.
//
// Root cause (scripts/audit_layout.py over the REAL authored rectangles in
// src/app/main.cpp): every control was laid out with hand-written pixel rects
// sized for one font at one DPI in one language. Three STATIC labels word-wrap
// to two lines but were authored one line tall, so the second line is CLIPPED
// (a STATIC is not scrollable and not ellipsized — the text is simply gone);
// the "Chế độ xuất" group box hangs 8 px past the page bottom; and nine tab
// headers need ~604 px inside a 528 px tab control, so Windows shrinks and
// clips the last labels ("Phòng Chaos", "AI Rival", "Tiến trình").
//
// The fix is DialogLayout.hpp: MEASURE at runtime (DrawTextW DT_CALCRECT with
// the real font on the real monitor — done in the Win32 layer) and let this
// solver reflow. This file feeds the solver the AUTHORED rectangles copied
// verbatim from main.cpp and the audit's measured text heights, and proves:
//
//   1. the authored layout really clips (the bug reproduces),
//   2. autoFit() grows exactly the clipped labels, pushes everything below
//      down by the same delta (columns preserved), stretches the enclosing
//      group box, and reports the extra window height the caller must add,
//   3. after autoFit() nothing is clipped and nothing overlaps,
//   4. planTabs() never clips the nine headers — it falls back to short
//      labels or multiple rows, and picks one-row-full labels when they fit,
//   5. growWindow() clamps to the monitor work area and reports the shortfall.
//
// The solver consumes measured numbers; it never guesses a font metric, so
// these assertions hold for any font/DPI/language the runtime measures.
//----------------------------------------------------------------------------
#include "DialogLayout.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace ok::layout;

namespace {

// Authored 96-DPI rectangles copied verbatim from src/app/main.cpp WM_CREATE
// (the S(n) macro is identity at 96 DPI). Page area is (16,88)-(544,568).
constexpr int kPageTop    = 88;
constexpr int kPageBottom = 568;

// Measured text heights (DrawTextW DT_CALCRECT, 13 px Segoe UI). A two-line
// wrap at this font is 32 px; the audit found these three authored too short.
constexpr int kTwoLineHeight = 32;

ControlSpec spec(int id, int x, int y, int w, int h, int tab,
                 int requiredHeight = 0, bool growable = false,
                 bool groupBox = false) {
    ControlSpec c;
    c.id = id; c.rect = Rect{x, y, w, h}; c.tab = tab;
    c.requiredHeight = requiredHeight; c.growable = growable; c.groupBox = groupBox;
    return c;
}

// IDs (symbolic — only identity/equality matters to the solver).
enum {
    GRP_METHOD = 1000, RADIO_TELEX, RADIO_VNI, RADIO_SIMPLETELEX,
    STAT_METHOD_HINT, STAT_CODETABLE, COMBO_CODETABLE,
    GRP_OPTIONS, CHK_DIGITS, CHK_SPELL, CHK_RESTORE, CHK_QUICK,
    CHK_MODERN, CHK_UPPER, CHK_MACRO,
    GRP_OUTPUT, RADIO_OUT_AUTO, RADIO_OUT_TSF, RADIO_OUT_SEND,
    STAT_OUT_NOTE, STAT_PERF_LAB, COMBO_PERF, CHK_PERF_LOWCPU,
    CHK_PERF_DICT, STAT_PERF_NOTE, CHK_NOTIFY
};

// The real "Bàn phím" page (tab 0) exactly as authored in main.cpp.
std::vector<ControlSpec> keyboardPage() {
    return {
        spec(GRP_METHOD, 24, 100, 494, 76, 0, 0, false, true),
        spec(RADIO_TELEX, 44, 122, 74, 20, 0),
        spec(RADIO_VNI, 128, 122, 58, 20, 0),
        spec(RADIO_SIMPLETELEX, 196, 122, 112, 20, 0),
        // Two-line label authored 26 px tall -> clips (needs 32).
        spec(STAT_METHOD_HINT, 44, 146, 460, 26, 0, kTwoLineHeight, true),
        spec(STAT_CODETABLE, 28, 186, 90, 18, 0),
        // Combo authored height is the DROP-DOWN height (200); the closed
        // control occupies ~20 px of layout, which is what the overlap
        // audit must use (mirrors audit_layout.py COMBO_CLOSED_H and the
        // real wiring, which measures the closed height).
        spec(COMBO_CODETABLE, 128, 182, 210, 20, 0),
        spec(GRP_OPTIONS, 24, 210, 494, 180, 0, 0, false, true),
        spec(CHK_DIGITS, 44, 218, 460, 20, 0),
        spec(CHK_SPELL, 44, 240, 460, 20, 0),
        spec(CHK_RESTORE, 44, 262, 460, 20, 0),
        spec(CHK_QUICK, 44, 284, 460, 20, 0),
        spec(CHK_MODERN, 44, 306, 460, 20, 0),
        spec(CHK_UPPER, 44, 328, 460, 20, 0),
        spec(CHK_MACRO, 44, 350, 460, 20, 0),
        // Group box authored to bottom 576 — 8 px past the 568 page bottom.
        spec(GRP_OUTPUT, 24, 400, 494, 176, 0, 0, false, true),
        spec(RADIO_OUT_AUTO, 44, 422, 66, 20, 0),
        spec(RADIO_OUT_TSF, 118, 422, 180, 20, 0),
        spec(RADIO_OUT_SEND, 44, 446, 240, 20, 0),
        spec(STAT_OUT_NOTE, 44, 468, 460, 28, 0, kTwoLineHeight, true),
        spec(STAT_PERF_LAB, 44, 502, 110, 18, 0),
        spec(COMBO_PERF, 158, 498, 180, 20, 0),   // closed height (drop-down was 160)
        spec(CHK_PERF_LOWCPU, 346, 499, 80, 20, 0),
        spec(CHK_PERF_DICT, 430, 499, 80, 20, 0),
        spec(STAT_PERF_NOTE, 44, 522, 460, 28, 0, kTwoLineHeight, true),
        spec(CHK_NOTIFY, 44, 552, 460, 20, 0),
    };
}

int indexOf(const std::vector<ControlSpec>& v, int id) {
    for (std::size_t i = 0; i < v.size(); ++i) { if (v[i].id == id) { return static_cast<int>(i); } }
    return -1;
}

void testAuthoredLayoutReproducesTheBug() {
    const std::vector<ControlSpec> page = keyboardPage();
    // (1) Three labels are authored shorter than their measured two-line text:
    //     the second line is clipped with no way for the user to reveal it.
    int clipped = 0;
    for (const ControlSpec& c : page) {
        if (c.requiredHeight > c.rect.h) { ++clipped; }
    }
    assert(clipped == 3);
    // (2) The output group box hangs past the page bottom (out-of-page).
    const int grpOut = indexOf(page, GRP_OUTPUT);
    assert(page[grpOut].rect.bottom() > kPageBottom);   // 576 > 568
    const int notify = indexOf(page, CHK_NOTIFY);
    assert(page[notify].rect.bottom() > kPageBottom);   // 572 > 568
    std::cout << "  [PASS] authored layout reproduces the bug (3 clipped labels,"
                 " group box + checkbox past the page bottom)\n";
}

void testAutoFitGrowsLabelsAndReflows() {
    const std::vector<ControlSpec> page = keyboardPage();
    const LayoutPlan plan = autoFit(page, kPageTop, kPageBottom);

    // Exactly the three clipped labels grew, by exactly their shortfall.
    assert(plan.grownControls == 3);
    assert(plan.totalGrowthPx == (32 - 26) + (32 - 28) + (32 - 28));   // 14

    const int hint = indexOf(page, STAT_METHOD_HINT);
    const int outN = indexOf(page, STAT_OUT_NOTE);
    const int perfN = indexOf(page, STAT_PERF_NOTE);
    assert(plan.rects[hint].h == kTwoLineHeight);
    assert(plan.rects[outN].h == kTwoLineHeight);
    assert(plan.rects[perfN].h == kTwoLineHeight);

    // Nothing is clipped after the solve, and the labels kept their authored
    // x/width (only ever moved DOWN, never sideways — columns stay intact).
    assert(plan.clippedIds.empty());
    assert(plan.rects[hint].x == page[hint].rect.x);
    assert(plan.rects[hint].w == page[hint].rect.w);

    // A control below the first grown label shifted down by that label's growth
    // (6 px): STAT_CODETABLE was at y=186, METHOD_HINT (y=146) grew 26->32.
    const int codeTbl = indexOf(page, STAT_CODETABLE);
    assert(plan.rects[codeTbl].y == 186 + 6);

    // The enclosing group box stretched to keep containing its grown child.
    const int grpMethod = indexOf(page, GRP_METHOD);
    assert(plan.rects[grpMethod].h > page[grpMethod].rect.h);
    assert(contains(plan.rects[grpMethod], plan.rects[hint]));

    // The page now needs extra height — the caller grows the window by this.
    assert(plan.extraHeightPx > 0);
    assert(plan.contentBottom == kPageBottom + plan.extraHeightPx);

    // And the reflowed page has NO overlapping controls (the user's "bị đè").
    const std::vector<OverlapPair> overlaps = findOverlaps(page, &plan.rects);
    assert(overlaps.empty());
    std::cout << "  [PASS] autoFit grows the 3 clipped labels, reflows below them,"
                 " stretches the group box, reports extra height, no overlaps\n";
}

void testAutoFitIsIdempotentAndMonotone() {
    // Re-solving an already-solved page (requiredHeight now fits) changes
    // nothing: the solver never shrinks and never double-shifts.
    const std::vector<ControlSpec> page = keyboardPage();
    const LayoutPlan first = autoFit(page, kPageTop, kPageBottom);

    std::vector<ControlSpec> solved = page;
    for (std::size_t i = 0; i < solved.size(); ++i) { solved[i].rect = first.rects[i]; }
    const LayoutPlan second = autoFit(solved, kPageTop, kPageBottom + first.extraHeightPx);
    assert(second.grownControls == 0);
    assert(second.totalGrowthPx == 0);
    assert(second.extraHeightPx == 0);
    for (std::size_t i = 0; i < solved.size(); ++i) {
        assert(second.rects[i].y == first.rects[i].y);
        assert(second.rects[i].h == first.rects[i].h);
    }
    std::cout << "  [PASS] autoFit is idempotent (a solved page re-solves unchanged)\n";
}

void testPlanTabsNeverClipsNineHeaders() {
    // Measured label widths for the nine real headers (13 px Segoe UI). Full
    // labels total ~604 px with padding — more than the 528 px display area.
    const std::vector<int> full = {58, 58, 44, 64, 62, 46, 78, 54, 68};
    const std::vector<int> shortLbl = {30, 24, 24, 30, 28, 46, 38, 16, 36};
    const int padding = 8, rowHeight = 22, displayInset = 4;

    // (a) Nine full labels in 528 px do NOT fit one row. With short labels
    //     provided and fitting, the solver keeps one row and switches to short.
    const TabPlan shortFit = planTabs(full, shortLbl, 528, padding, rowHeight, displayInset);
    assert(shortFit.rows == 1);
    assert(shortFit.useShortLabels);
    assert(!shortFit.multiline);
    assert(shortFit.requiredWidth <= 528);

    // (b) No short labels available -> the only honest option is multiple rows
    //     (the caller creates the tab with TCS_MULTILINE). Never clip.
    const TabPlan multi = planTabs(full, {}, 528, padding, rowHeight, displayInset);
    assert(multi.rows >= 2);
    assert(multi.multiline);
    assert(multi.controlHeight == displayInset + multi.rows * rowHeight);

    // (c) A widened dialog (the other fix the caller may choose) fits all nine
    //     full labels on one row.
    const TabPlan wide = planTabs(full, shortLbl, 700, padding, rowHeight, displayInset);
    assert(wide.rows == 1);
    assert(!wide.useShortLabels);
    assert(wide.requiredWidth <= 700);

    // In every case the chosen labels fit the chosen geometry (no clipping).
    for (const TabPlan* p : {&shortFit, &multi, &wide}) {
        if (p->rows == 1) { assert(p->requiredWidth <= p->availableWidth); }
    }
    std::cout << "  [PASS] planTabs never clips nine headers (short labels /"
                 " multi-row / widened dialog all resolve)\n";
}

void testGrowWindowClampsToWorkArea() {
    // The dialog client is 622 px tall; autoFit asked for `extra`.
    const WindowGrowth fits = growWindow(622, 40, 1000);
    assert(fits.newClientHeight == 662);
    assert(fits.unsatisfiedPx == 0);

    // A short monitor work area clamps the growth and reports the shortfall,
    // which autoFit then surfaces as clipped controls (honest, never silent).
    const WindowGrowth clamped = growWindow(622, 200, 700);
    assert(clamped.newClientHeight == 700);
    assert(clamped.unsatisfiedPx == 122);

    // No request -> no change.
    const WindowGrowth none = growWindow(622, 0, 1000);
    assert(none.newClientHeight == 622 && none.unsatisfiedPx == 0);
    std::cout << "  [PASS] growWindow clamps to the work area and reports the shortfall\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running Dialog Layout Solver Suite ===\n";
    testAuthoredLayoutReproducesTheBug();
    testAutoFitGrowsLabelsAndReflows();
    testAutoFitIsIdempotentAndMonotone();
    testPlanTabsNeverClipsNineHeaders();
    testGrowWindowClampsToWorkArea();
    std::cout << "=== ALL DIALOG LAYOUT TESTS PASSED ===\n";
    return 0;
}
