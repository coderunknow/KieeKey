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
#include <cmath>
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

//===========================================================================
// v1.3.0-beta5 (bug B1) — the real-Windows regression battery.
//
// The beta4 solver was wired in and its page maths unit-tested, yet the
// dialog STILL overlapped/clipped on real Windows. Root causes found by
// auditing the shipped wiring against this model:
//   (a) autoFit() counted the always-visible chrome (the button row is
//       authored BELOW the page viewport) into contentBottom, so
//       extraHeightPx was permanently ~42 px > 0 — the window grew on every
//       open even with nothing clipped;
//   (b) the caller applied growth ALL-OR-NOTHING: when the work area could
//       not satisfy it, NOTHING was applied — but the page children had
//       already moved to their solved rects, so they overlapped the unmoved
//       button row and clipped at the old window bottom (worst at 125-150 %
//       scaling on small screens — exactly the tester's report);
//   (c) the window was never repositioned into the work area after growing,
//       so even "satisfied" growth could hang below the screen edge;
//   (d) there was no scroll fallback for content that genuinely does not fit.
// The tests below pin (a) here and (b)-(d) through refitWindow()/
// fitRectToWorkArea()/scrollChildRect(), across 100/125/150/200 % scale
// factors with realistic measured metrics including WIDE Vietnamese labels.
//===========================================================================

// The real dialog chrome at 96 dpi (authored): window client 560x622, the
// non-client chrome (caption + borders) measures ~39 px tall on Windows 10/11
// at 96 dpi and scales with the frame; page viewport bottom ~568.
constexpr int kChromePx96   = 39;
constexpr int kClientW96    = 560;
constexpr int kClientH96    = 622;
constexpr int kViewportBottom96 = 568;

// v1.3.0-beta5: the SHIPPED (beta4 re-fit) "Bàn phím" page. Nothing here is
// clipped at 96 dpi (the deepest bottom is the output group box at 566,
// inside the 568 viewport) — so a correct solver reports ZERO extra height
// for it. The beta4 solver reported ~42 px anyway because it counted the
// always-visible button row (authored BELOW the viewport) as page content.
std::vector<ControlSpec> keyboardPageShipped() {
    return {
        spec(GRP_METHOD, 24, 100, 494, 84, 0, 0, false, true),
        spec(RADIO_TELEX, 44, 122, 74, 20, 0),
        spec(RADIO_VNI, 128, 122, 58, 20, 0),
        spec(RADIO_SIMPLETELEX, 196, 122, 112, 20, 0),
        spec(STAT_METHOD_HINT, 44, 146, 460, 34, 0, kTwoLineHeight, true),
        spec(STAT_CODETABLE, 28, 190, 90, 18, 0),
        spec(COMBO_CODETABLE, 128, 186, 210, 20, 0),
        spec(GRP_OPTIONS, 24, 214, 494, 168, 0, 0, false, true),
        spec(CHK_DIGITS, 44, 218, 460, 20, 0),
        spec(CHK_SPELL, 44, 240, 460, 20, 0),
        spec(CHK_RESTORE, 44, 262, 460, 20, 0),
        spec(CHK_QUICK, 44, 284, 460, 20, 0),
        spec(CHK_MODERN, 44, 306, 460, 20, 0),
        spec(CHK_UPPER, 44, 328, 460, 20, 0),
        spec(CHK_MACRO, 44, 350, 460, 20, 0),
        spec(GRP_OUTPUT, 24, 388, 494, 178, 0, 0, false, true),
        spec(RADIO_OUT_AUTO, 44, 410, 66, 20, 0),
        spec(RADIO_OUT_TSF, 118, 410, 180, 20, 0),
        spec(RADIO_OUT_SEND, 44, 432, 240, 20, 0),
        spec(STAT_OUT_NOTE, 44, 454, 460, 34, 0, kTwoLineHeight, true),
        spec(STAT_PERF_LAB, 44, 492, 110, 18, 0),
        spec(COMBO_PERF, 158, 488, 180, 20, 0),   // closed height
        spec(CHK_PERF_LOWCPU, 346, 489, 80, 20, 0),
        spec(CHK_PERF_DICT, 430, 489, 80, 20, 0),
        spec(STAT_PERF_NOTE, 44, 516, 460, 34, 0, kTwoLineHeight, true),
    };
}

// A whole-dialog spec set at one scale factor: the SHIPPED keyboard page +
// the always-visible chrome (header rows above the viewport, button row
// below), exactly like the real WM_CREATE wiring feeds the solver (EVERY
// child, chrome included). `extraWrapPx` simulates the runtime measurement
// finding a label that wraps taller than authored at this scale (font
// hinting/rounding at 125-200 % — the measurement, never a guess, drives it).
std::vector<ControlSpec> dialogSpecsAt(int scalePct, int extraWrapPx) {
    const auto S = [scalePct](int px) { return px * scalePct / 100; };
    std::vector<ControlSpec> page = keyboardPageShipped();
    for (auto& c : page) {
        c.rect = Rect{S(c.rect.x), S(c.rect.y), S(c.rect.w), S(c.rect.h)};
        if (c.growable && c.requiredHeight > 0) {
            c.requiredHeight = S(c.requiredHeight);
        }
    }
    if (extraWrapPx > 0) {
        const int note = indexOf(page, STAT_PERF_NOTE);
        page[static_cast<std::size_t>(note)].requiredHeight =
            page[static_cast<std::size_t>(note)].rect.h + S(extraWrapPx);
    }
    std::vector<ControlSpec> all;
    all.push_back(spec(9001, S(14), S(10), S(34), S(34), ControlSpec::kAlwaysVisible));
    all.push_back(spec(9002, S(58), S(12), S(360), S(28), ControlSpec::kAlwaysVisible));
    all.push_back(spec(9003, S(58), S(42), S(490), S(18), ControlSpec::kAlwaysVisible));
    for (auto& c : page) { all.push_back(c); }
    all.push_back(spec(9010, S(12), S(580), S(240), S(30), ControlSpec::kAlwaysVisible));
    all.push_back(spec(9011, S(300), S(580), S(76), S(30), ControlSpec::kAlwaysVisible));
    all.push_back(spec(9012, S(384), S(580), S(76), S(30), ControlSpec::kAlwaysVisible));
    all.push_back(spec(9013, S(468), S(580), S(80), S(30), ControlSpec::kAlwaysVisible));
    return all;
}

void testAlwaysVisibleChromeNeverDrivesGrowth() {
    // (a): with NOTHING clipped, extraHeightPx must be 0 even though the
    // button row (always-visible) is authored below the page viewport. The
    // beta4 solver reported ~42 px here and grew the window on every open —
    // and on small work areas the all-or-nothing growth application then
    // skipped the growth entirely while children had already moved.
    for (const int scale : {100, 125, 150, 200}) {
        const std::vector<ControlSpec> all = dialogSpecsAt(scale, 0);
        const int pageTop = 88 * scale / 100;
        const int pageBottom = kViewportBottom96 * scale / 100;
        const LayoutPlan plan = autoFit(all, pageTop, pageBottom);
        assert(plan.extraHeightPx == 0);
        assert(plan.grownControls == 0);
        // The chrome rects must be untouched by the solve.
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].tab == ControlSpec::kAlwaysVisible) {
                assert(plan.rects[i].y == all[i].rect.y);
            }
        }
        // And nothing overlaps on any page at any scale.
        assert(findOverlaps(all, &plan.rects).empty());
    }
    std::cout << "  [PASS] always-visible chrome never drives page growth"
                 " (100/125/150/200 %)\n";
}

void testRefitWindowAppliesPartialGrowthAndScroll() {
    // (b): partial-growth repro. At 125 % the authored dialog is 826 px tall
    // (client 777 + chrome 49) and a wrapping label adds ~27 px more; the
    // work area (1366x840) fits the authored window but NOT the wanted one —
    // beta4 applied NOTHING here (all-or-nothing), leaving the moved page
    // children overlapping the unmoved button row with no way to scroll.
    const int scale = 125;
    const std::vector<ControlSpec> all = dialogSpecsAt(scale, 40);
    const int pageTop = 88 * scale / 100;
    const int pageBottom = kViewportBottom96 * scale / 100;
    const LayoutPlan plan = autoFit(all, pageTop, pageBottom);
    assert(plan.extraHeightPx > 0);         // the page genuinely needs room

    const Rect window{100, 60, kClientW96 * scale / 100,
                      kClientH96 * scale / 100 + kChromePx96 * scale / 100};
    const int clientH = kClientH96 * scale / 100;
    const Rect work{0, 0, 1366, 840};
    const WindowRefit fit = refitWindow(window, clientH, pageBottom,
                                        pageBottom + plan.extraHeightPx, work);
    // The growth is applied up to the cap — NEVER all-or-nothing.
    assert(fit.clientDelta > 0);
    assert(fit.windowRect.h == window.h + fit.clientDelta);
    assert(fit.windowRect.h <= work.h);
    // What did not fit is reachable through the scroll fallback...
    assert(fit.scrollNeeded);
    assert(fit.scrollRange == plan.extraHeightPx - fit.clientDelta);
    // ...and the window ends fully inside the work area (c).
    assert(fit.windowRect.y >= work.y);
    assert(fit.windowRect.bottom() <= work.bottom());

    // Idempotence: re-solving the refitted window against the same work area
    // changes nothing (the second pass sees the grown viewport and the same
    // remaining scroll range; no runaway growth).
    const WindowRefit again = refitWindow(
        fit.windowRect, clientH + fit.clientDelta, pageBottom + fit.clientDelta,
        pageBottom + plan.extraHeightPx, work);
    assert(again.clientDelta == 0);
    assert(again.scrollRange == fit.scrollRange);
    std::cout << "  [PASS] refitWindow applies partial growth + scroll range"
                 " (125 % on an 840 px work area), idempotent\n";
}

void testRefitWindowGrowsFullyWhenItFits() {
    // The comfortable case: 100 % on a 1080p work area — everything the page
    // wants is applied, no scroll needed.
    const std::vector<ControlSpec> all = dialogSpecsAt(100, 30);
    const LayoutPlan plan = autoFit(all, 88, kViewportBottom96);
    assert(plan.extraHeightPx > 0);
    const Rect window{200, 100, kClientW96, kClientH96 + kChromePx96};
    const Rect work{0, 0, 1920, 1040};
    const WindowRefit fit = refitWindow(window, kClientH96, kViewportBottom96,
                                        kViewportBottom96 + plan.extraHeightPx, work);
    assert(fit.clientDelta == plan.extraHeightPx);
    assert(!fit.scrollNeeded && fit.scrollRange == 0);
    assert(fit.windowRect.bottom() <= work.bottom());
    // No-growth case: an unclipped page leaves the window exactly as authored.
    const std::vector<ControlSpec> flat = dialogSpecsAt(100, 0);
    const LayoutPlan planFlat = autoFit(flat, 88, kViewportBottom96);
    const WindowRefit fitFlat = refitWindow(window, kClientH96, kViewportBottom96,
                                            kViewportBottom96 + planFlat.extraHeightPx,
                                            work);
    assert(fitFlat.clientDelta == 0 && !fitFlat.scrollNeeded);
    assert(fitFlat.windowRect.h == window.h);
    std::cout << "  [PASS] refitWindow grows fully when the work area allows"
                 " and not at all when nothing is clipped\n";
}

void testRefitWindowShrinksOversizedWindowToScroll() {
    // (b/d): 150 % on a 768-px-tall screen — the AUTHORED window alone is
    // already taller than the work area. The model must shrink it into view
    // and expose the overflow as scroll range (beta4 left it hanging off the
    // screen with the button row unreachable).
    const int scale = 150;
    const std::vector<ControlSpec> all = dialogSpecsAt(scale, 0);
    const int pageBottom = kViewportBottom96 * scale / 100;   // 852
    const LayoutPlan plan = autoFit(all, 88 * scale / 100, pageBottom);
    const int clientH = kClientH96 * scale / 100;             // 933
    const int chromeH = kChromePx96 * scale / 100;
    const Rect window{50, 40, kClientW96 * scale / 100, clientH + chromeH};
    const Rect work{0, 0, 1366, 728};
    const WindowRefit fit = refitWindow(window, clientH, pageBottom,
                                        pageBottom + plan.extraHeightPx, work);
    assert(fit.clientDelta < 0);                    // SHRINK into the work area
    assert(fit.windowRect.h <= work.h);
    assert(fit.windowRect.bottom() <= work.bottom());
    assert(fit.scrollNeeded);
    // Everything below the new viewport bottom is scroll-reachable.
    assert(fit.scrollRange == plan.extraHeightPx - fit.clientDelta);
    std::cout << "  [PASS] refitWindow shrinks an oversized window and keeps"
                 " the overflow scrollable (150 % on 728 px)\n";
}

void testFitRectToWorkArea() {
    const Rect work{0, 0, 1366, 728};
    // Already inside: no movement.
    const Rect inside{100, 100, 500, 400};
    assert(fitRectToWorkArea(inside, work).x == 100);
    assert(fitRectToWorkArea(inside, work).y == 100);
    // Hanging below the bottom edge: slides up, size unchanged.
    const Rect low{100, 500, 500, 400};
    const Rect fixedLow = fitRectToWorkArea(low, work);
    assert(fixedLow.h == 400 && fixedLow.bottom() == work.bottom());
    // Taller than the work area: clamped AND aligned to the top.
    const Rect huge{100, 300, 500, 900};
    const Rect fixedHuge = fitRectToWorkArea(huge, work);
    assert(fixedHuge.h == work.h && fixedHuge.y == work.y);
    // Off to the right: slides left.
    const Rect right{1200, 100, 400, 300};
    assert(fitRectToWorkArea(right, work).right() == work.right());
    std::cout << "  [PASS] fitRectToWorkArea keeps a window fully on screen\n";
}

void testScrollChildRectClipsAtViewportEdges() {
    const Rect viewport{16, 88, 528, 480};      // x,y,w,h -> bottom 568
    // A child fully inside just moves up by the offset, no clip.
    const ScrolledChild inside = scrollChildRect(Rect{44, 300, 460, 32}, 100, viewport);
    assert(inside.visible && !inside.clipped);
    assert(inside.rect.y == 200);
    // A child straddling the TOP edge is clipped to it (child-local rect).
    const ScrolledChild top = scrollChildRect(Rect{44, 120, 460, 100}, 100, viewport);
    assert(top.visible && top.clipped);
    assert(top.rect.y == 20);
    assert(top.clip.y == viewport.y - top.rect.y);
    assert(top.clip.h == top.rect.h - top.clip.y);
    // A child straddling the BOTTOM edge is clipped to it.
    const ScrolledChild bottom = scrollChildRect(Rect{44, 600, 460, 100}, 100, viewport);
    assert(bottom.visible && bottom.clipped);
    assert(bottom.clip.y == 0);
    assert(bottom.clip.h == viewport.bottom() - bottom.rect.y);
    // A child scrolled entirely past the top is hidden (empty region), and
    // one entirely below the viewport too — showTab()'s SW_SHOW/SW_HIDE
    // contract is never touched.
    const ScrolledChild gone = scrollChildRect(Rect{44, 80, 460, 100}, 100, viewport);
    assert(!gone.visible && gone.clip.h == 0);
    const ScrolledChild below = scrollChildRect(Rect{44, 700, 460, 100}, 0, viewport);
    assert(!below.visible);
    // Scroll metrics are covered by the dedicated BS-01 test below: the old
    // expectations that used to live here (`rangeMax == overflow`,
    // `pagePx == viewport * 9 / 10`) ENCODED THE BUG — see
    // testScrollMetricsEnableTheBarExactlyWhenContentOverflows().
    std::cout << "  [PASS] scrollChildRect clips at the viewport edges\n";
}

void testDpiSweepEveryControlInsideOrScrollable() {
    // The acceptance battery: at 100/125/150/200 % with realistic measured
    // metrics (including a wide VN label wrapping to three lines), after
    // solve + refit + (scroll at offset 0 AND at the range max), EVERY page
    // control is either fully inside the visible client area or reachable
    // through the scroll range — none is silently lost below an unmoved
    // window edge, and the button row never overlaps page content.
    struct Screen { int scale; int workH; };
    const Screen screens[] = {{100, 1040}, {125, 900}, {150, 728}, {200, 700}};
    for (const Screen& s : screens) {
        const std::vector<ControlSpec> all = dialogSpecsAt(s.scale, 24);
        const int pageTop = 88 * s.scale / 100;
        const int pageBottom = kViewportBottom96 * s.scale / 100;
        const LayoutPlan plan = autoFit(all, pageTop, pageBottom);
        assert(plan.clippedIds.empty());            // the solve itself clips nothing
        const int clientH = kClientH96 * s.scale / 100;
        const int chromeH = kChromePx96 * s.scale / 100;
        const Rect window{80, 50, kClientW96 * s.scale / 100, clientH + chromeH};
        const Rect work{0, 0, 1366, s.workH};
        const WindowRefit fit = refitWindow(window, clientH, pageBottom,
                                            pageBottom + plan.extraHeightPx, work);
        assert(fit.windowRect.bottom() <= work.bottom());
        const Rect viewport{16 * s.scale / 100, pageTop,
                            (544 - 16) * s.scale / 100,
                            (pageBottom - pageTop) + fit.clientDelta};
        // The button row moved by exactly clientDelta — never overlapping a
        // page control (they all end at or above the new viewport bottom).
        const int rowTop = 580 * s.scale / 100 + fit.clientDelta;
        assert(rowTop >= viewport.bottom());
        // At offset 0 and at the range max, every page control is visible or
        // clipped-but-reachable; at range max the DEEPEST control's bottom is
        // inside the viewport.
        const int offsets[] = {0, fit.scrollRange};
        int deepestBottom = 0;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].tab == ControlSpec::kAlwaysVisible) { continue; }
            deepestBottom = std::max(deepestBottom, plan.rects[i].bottom());
        }
        for (const int off : offsets) {
            for (std::size_t i = 0; i < all.size(); ++i) {
                if (all[i].tab == ControlSpec::kAlwaysVisible) { continue; }
                const ScrolledChild sc = scrollChildRect(plan.rects[i], off, viewport);
                if (off == fit.scrollRange && sc.visible) {
                    // At full scroll nothing may hang below the viewport.
                    assert(sc.rect.y + sc.clip.h <= viewport.bottom() || sc.clipped);
                }
                (void)sc;
            }
        }
        // The deepest content is reachable at full scroll.
        assert(deepestBottom - fit.scrollRange <= viewport.bottom());
    }
    std::cout << "  [PASS] DPI sweep 100/125/150/200 %: every control inside"
                 " the client area or scroll-reachable, no chrome overlap\n";
}

void testScrollMetricsEnableTheBarExactlyWhenContentOverflows() {
    // v1.3.0-beta8 (bug BS-01) — "chữ bị che, không thể kéo".
    //
    // The scroll fallback shipped (beta5, bug B1) as
    //     nMax  = overflow - 1          (overflow == content below the viewport)
    //     nPage = 90 % of the viewport
    // Win32 DISABLES a scrollbar whenever nPage >= nMax + 1. With a viewport of
    // ~470 px nPage is ~423, while the overflows this dialog actually produces
    // are tens of pixels (one wrapped label = +17 px). 423 >= 18 => the bar was
    // dead in exactly the cases it exists for, while applySettingsScrollOffset()
    // had already region-clipped the children below the viewport: the text was
    // invisible AND unreachable.
    //
    // Correct model: 1 scroll unit = 1 px, nMax + 1 == the whole content,
    // nPage == the viewport. The bar is then enabled iff there IS overflow,
    // the thumb fraction is viewport/content (as every native scrollbar shows
    // it) and the drag travel is exactly the overflow.
    const int viewports[] = {300, 470};
    const int overflows[] = {1, 15, 150, 1000};
    for (const int v : viewports) {
        const ScrollMetrics fits = scrollMetrics(v, 0);
        assert(!fits.enabled);                      // nothing to scroll => disabled
        assert(fits.pagePx == v);
        assert(fits.maxTravelPx == 0);
        assert(fits.contentPx == v);
        assert(fits.rangeMaxPx + 1 == v);           // nMax + 1 == content
        assert(fits.thumbFraction() == 1.0);
        for (const int r : overflows) {
            const ScrollMetrics m = scrollMetrics(v, r);
            assert(m.enabled);                      // <-- RED on the old formula
            assert(m.pagePx == v);                  // one page == the viewport
            assert(m.maxTravelPx == r);             // drag travel == the overflow
            assert(m.contentPx == v + r);
            assert(m.rangeMaxPx == m.contentPx - 1);
            // The Win32 enable condition, verbatim: nPage < nMax + 1.
            assert(m.pagePx < m.rangeMaxPx + 1);
            // Thumb fraction == viewport / content (what the user sees).
            const double expected = static_cast<double>(v) / static_cast<double>(v + r);
            assert(std::fabs(m.thumbFraction() - expected) < 1e-9);
            // The thumb travel maps exactly onto the scroll range.
            assert(std::fabs((1.0 - m.thumbFraction()) * m.contentPx - r) < 1e-9);
        }
        // A negative range (model misuse) must clamp to "no scrolling", never
        // to a negative nMax that would make the bar's position meaningless.
        const ScrollMetrics neg = scrollMetrics(v, -5);
        assert(!neg.enabled && neg.maxTravelPx == 0 && neg.rangeMaxPx + 1 == v);
        // A degenerate viewport (measured before the first solve) must never
        // produce nPage == 0 — Win32 treats that as "no page info" and hides
        // the thumb entirely.
        const ScrollMetrics tiny = scrollMetrics(0, 40);
        assert(tiny.pagePx >= 1 && tiny.enabled);
        assert(tiny.pagePx < tiny.rangeMaxPx + 1);
    }
    std::cout << "  [PASS] BS-01: scrollbar enabled iff content overflows;"
                 " thumb fraction == viewport/content; travel == overflow\n";
}

void testScrollOffsetIsAlwaysReachableAtBothEnds() {
    // BS-01 follow-through: with the corrected metrics the LAST line of a page
    // is reachable at max offset for every (viewport, overflow) pair — the
    // property the user experiences as "I can finally read the bottom of the
    // tab". Previously maxTravel was capped at nMax == overflow - 1, so the
    // final pixel row of content was never reachable even with a live bar.
    const int viewports[] = {300, 470};
    const int overflows[] = {1, 15, 150, 1000};
    for (const int v : viewports) {
        for (const int r : overflows) {
            const ScrollMetrics m = scrollMetrics(v, r);
            const int contentBottom = v + r;                 // in viewport coords
            const int lastVisibleRow = contentBottom - m.maxTravelPx;
            assert(lastVisibleRow == v);                     // bottom row == viewport bottom
            // Every offset in [0, maxTravel] is a legal, non-negative position
            // and never overshoots the content.
            for (int off = 0; off <= m.maxTravelPx; off += std::max(1, m.maxTravelPx / 7)) {
                assert(off >= 0 && off <= r);
                assert(contentBottom - off >= v);
            }
        }
    }
    std::cout << "  [PASS] BS-01: the deepest content row is reachable at max"
                 " offset for every viewport/overflow pair\n";
}

} // namespace

// v1.3.0-beta8 (bug BS-09): the bottom chrome row slides DOWN and keeps its X.
// The beta7 apply loop called SetWindowPos(..., 0, y, 0, 0, SWP_NOSIZE | ...) —
// no SWP_NOMOVE — so all four bottom buttons jumped to x=0 and stacked as soon
// as the dialog grew. The probe caught it on real Windows; this is the portable
// guard (see scratch/BS09 seed note in BUG_HUNT_REPORT_beta8...).
void testBottomRowMovesDownOnly() {
    // The row is chrome: the solver must never treat it as page content.
    assert(ControlSpec::kAlwaysVisible == -1);
    const ok::layout::Rect toggle{12, 580, 240, 30};
    const ok::layout::Rect okBtn{300, 580, 76, 30};
    const ok::layout::Rect cancel{384, 580, 76, 30};
    const ok::layout::Rect apply{468, 580, 80, 30};
    const ok::layout::Rect header{58, 12, 490, 18};
    const int tabBottom = 572;   // authored tab control: S(66) + S(506), 96 dpi
    const int slack = 4;
    const int delta = 67;        // the CI refit grew the client by 67 px (probe: y=580 -> 647)

    for (const ok::layout::Rect& r : {toggle, okBtn, cancel, apply}) {
        const ok::layout::BottomRowMove m =
            ok::layout::bottomRowMove(r, tabBottom, slack, delta);
        assert(m.moves);
        assert(m.rect.x == r.x);                 // BS-09: X is preserved
        assert(m.rect.y == r.y + delta);         // and Y follows the refit
        assert(m.rect.w == r.w && m.rect.h == r.h);
        // The row must stay a row: the four controls keep the authored gaps.
    }
    const ok::layout::BottomRowMove a =
        ok::layout::bottomRowMove(apply, tabBottom, slack, delta);
    const ok::layout::BottomRowMove t =
        ok::layout::bottomRowMove(toggle, tabBottom, slack, delta);
    assert(a.rect.x - (t.rect.x + t.rect.w) == 468 - (12 + 240));   // gap preserved

    // The header is ABOVE the tab bottom: it never moves with the refit.
    const ok::layout::BottomRowMove h =
        ok::layout::bottomRowMove(header, tabBottom, slack, delta);
    assert(!h.moves);
    assert(h.rect.x == header.x && h.rect.y == header.y);

    // A dialog that did not grow must not move the row at all.
    const ok::layout::BottomRowMove z =
        ok::layout::bottomRowMove(okBtn, tabBottom, slack, 0);
    assert(z.moves);
    assert(z.rect.y == okBtn.y && z.rect.x == okBtn.x);

    // A control exactly on the slack line counts as the row (>= tabBottom - slack).
    const ok::layout::Rect edge{44, tabBottom - slack, 100, 20};
    assert(ok::layout::bottomRowMove(edge, tabBottom, slack, delta).moves);
    const ok::layout::Rect above{44, tabBottom - slack - 1, 100, 20};
    assert(!ok::layout::bottomRowMove(above, tabBottom, slack, delta).moves);
}

// v1.3.0-beta8 (bug BS-10): a two-row tab strip must never hide the page's top.
// v1.3.0-beta8fix1 (bug BS-20) — A PAGE CHILD MAY NOT BE WIDER THAN THE PAGE.
//
// The 125 % pass of the UI probe (added by this release, because the regression
// contract names 100/125/150 %) measured a group box whose authored 496 px became
// 620 px after the DPI rescale, at x=30, in a 641 px client: 650 px of content in
// a 641 px dialog. That is `outside_page` by construction — and invisible at
// 100 %, where the same row happens to fit, which is why four green CI rounds and
// a green local suite never saw it. The clamp is the model's answer; this asserts
// its three properties at all three contract scales.
// v1.3.0-beta8fix1 (bug BS-21) — THE TAB STRIP RESHAPES, THE PAGE COMES BACK.
//
// The failing state the x64 probe recorded:
//
//   [I1] page is EMPTY at offset 0 (page 22,177 639x176): 580 36,383 259x1210 ...
//        tab 5 dpi 144 offset 0/1048 contentBottom[5] 1401
//
// — a tab whose controls sit 200+ px BELOW a 176 px page, none of them visible,
// while the app reports a coherent layout. The mechanism is the shift BS-10 added
// to keep the page clear of a multi-row strip: it is computed from the previous
// solve's baseline, and `pageTopShiftPx` can only push DOWN, so a strip that
// wrapped once kept the page low forever (`stripShift 228` px in a 494x497 page by
// the end of a fuzz walk). These assertions are the model's half of the fix:
// grow -> shrink -> grow three times, at every scale the contract names, must
// return the page to exactly its one-row position — and the accumulating rule the
// code used must be shown to fail them (non-vacuity).
// v1.3.0-beta8fix1 (bug BS-22) — THE SOLVE IS RECOMPUTED, NOT ACCUMULATED.
//
// The CI probe caught the additive behaviour with a plain sentence:
//
//     [I1] the page content did not return to its one-row position: id 555 was at
//          y=114, is at y=158 after the strip fitted one row again (moved by 44 px)
//
// The row that wrapped at the narrow width had grown, pushed everything below it
// down, and kept BOTH the height and the push when the dialog widened again —
// because the solver's input was the previous solve's output (the live/baseline
// rectangle), and `autoFit` only knows how to add growth. This test drives the
// exact cycle on the portable model: a growable label with a width-dependent
// required height, a sibling under it, a constrained page (the probe's 639x176
// shape, scaled), and the requirement that a re-solve from the AUTHORED layout
// returns the authored plan exactly — at every scale the contract names.
void testReflowIsRecomputedFromAuthored() {
    struct Scale { int dpi; };
    const Scale scales[] = {{96}, {120}, {144}};

    for (const Scale& sc : scales) {
        const int narrowW = dpiScalePx(320, sc.dpi);
        const int wideW   = dpiScalePx(640, sc.dpi);
        // The page the probe measured at 150 %: 22,177 639x176 — 176 px tall.
        const int pageTop = dpiScalePx(100, sc.dpi);
        const int pageBottom = pageTop + dpiScalePx(176, sc.dpi);

        const Rect authoredLabel{dpiScalePx(10, sc.dpi), pageTop,
                                 dpiScalePx(300, sc.dpi), dpiScalePx(20, sc.dpi)};
        const Rect authoredButton{dpiScalePx(10, sc.dpi), dpiScalePx(130, sc.dpi),
                                  dpiScalePx(300, sc.dpi), dpiScalePx(20, sc.dpi)};

        // The label's text: two lines at the narrow width, one at the wide one —
        // i.e. exactly what the app's own measurement reports, and the only input
        // the solver is entitled to use.
        const auto specsAt = [&](int width, bool authoredInput, const LayoutPlan* prev) {
            ControlSpec label;
            label.id = 1;
            label.tab = 0;
            label.growable = true;
            label.rect = authoredInput ? authoredLabel : prev->rects[0];
            label.requiredHeight = (width < dpiScalePx(400, sc.dpi))
                                       ? dpiScalePx(40, sc.dpi)   // two lines
                                       : dpiScalePx(20, sc.dpi);  // one line
            ControlSpec button;
            button.id = 2;
            button.tab = 0;
            button.rect = authoredInput ? authoredButton : prev->rects[1];
            button.requiredHeight = 0;
            std::vector<ControlSpec> v{label, button};
            return v;
        };

        // --- the narrow solve: the label grows, the button moves down ---------
        std::vector<ControlSpec> narrow = specsAt(narrowW, true, nullptr);
        const LayoutPlan narrowPlan = autoFit(narrow, pageTop, pageBottom);
        assert(narrowPlan.rects[0].h == dpiScalePx(40, sc.dpi));
        assert(narrowPlan.rects[1].y == dpiScalePx(150, sc.dpi));   // pushed down

        // --- the wide solve, from the AUTHORED layout: back to the authored plan -
        std::vector<ControlSpec> wide = specsAt(wideW, true, nullptr);
        const LayoutPlan widePlan = autoFit(wide, pageTop, pageBottom);
        assert(widePlan.rects[0].h == authoredLabel.h);              // shrank back
        assert(widePlan.rects[0].y == authoredLabel.y);
        assert(widePlan.rects[1].y == authoredButton.y);             // no push left
        assert(widePlan.rects[1].h == authoredButton.h);

        // --- and the accumulating rule must FAIL the same assertions ----------
        // (what the solver did when its input was the previous plan: `autoFit`
        // adds growth and cannot take it back, so both the height and the push
        // stay for the rest of the session).
        std::vector<ControlSpec> acc = specsAt(wideW, false, &narrowPlan);
        const LayoutPlan accPlan = autoFit(acc, pageTop, pageBottom);
        assert(accPlan.rects[0].h == dpiScalePx(40, sc.dpi));        // grew ... stayed
        assert(accPlan.rects[1].y == dpiScalePx(150, sc.dpi));       // push ... stayed
        assert(accPlan.rects[1].y != authoredButton.y);              // the defect

        // --- three full cycles, authored-in: no drift at all -------------------
        for (int cycle = 1; cycle <= 3; ++cycle) {
            std::vector<ControlSpec> n = specsAt(narrowW, true, nullptr);
            const LayoutPlan np = autoFit(n, pageTop, pageBottom);
            assert(np.rects[1].y == dpiScalePx(150, sc.dpi));
            std::vector<ControlSpec> w = specsAt(wideW, true, nullptr);
            const LayoutPlan wp = autoFit(w, pageTop, pageBottom);
            assert(wp.rects[0].h == authoredLabel.h);
            assert(wp.rects[1].y == authoredButton.y);
        }

        // --- the constrained page (639x176-style) keeps the content reachable ---
        // A page shorter than the content is a SCROLL situation, not a layout one:
        // the authored positions are the truth and the rest is travel.
        std::vector<ControlSpec> tall = specsAt(wideW, true, nullptr);
        const LayoutPlan tallPlan = autoFit(tall, pageTop, pageTop + dpiScalePx(40, sc.dpi));
        assert(tallPlan.rects[0].y == authoredLabel.y);              // not pushed up
        assert(tallPlan.rects[1].y == authoredButton.y);             // nor down
    }
}

void testStripShiftReturnsToAuthoredOnShrink() {
    struct Scale { int dpi; };
    const Scale scales[] = {{96}, {120}, {144}};
    // A constrained page at 150 %: the probe measured 22,177 639x176 — a short
    // client whose strip has wrapped. The authored page top is what it is at one
    // row; the wrapped strip pushes the viewport top DOWN.
    const int authoredTop96 = 100;          // first page control, one-row strip
    const int wrappedTop96  = 122;          // two rows of labels (+2 x 11 px)

    for (const Scale& sc : scales) {
        const int authoredTop = dpiScalePx(authoredTop96, sc.dpi);
        const int wrappedTop  = dpiScalePx(wrappedTop96, sc.dpi);

        int prevShift96 = 0;                 // the app's per-tab record
        int top = authoredTop;               // the page top as laid out

        // --- 1. one row: the shift is 0 and the page is at its authored top ---
        {
            StripShift s0 = stripShiftFor(top, prevShift96, authoredTop, sc.dpi);
            assert(s0.authoredTopPx == authoredTop);
            assert(s0.shiftPx == 0 && s0.shift96 == 0);
            top = s0.authoredTopPx + s0.shiftPx;
            prevShift96 = s0.shift96;
        }
        const int oneRowTop = top;
        assert(oneRowTop == authoredTop);

        for (int cycle = 1; cycle <= 3; ++cycle) {
            // --- 2. the strip wraps: the page moves down to clear it ----------
            {
                StripShift sw = stripShiftFor(top, prevShift96, wrappedTop, sc.dpi);
                assert(sw.authoredTopPx == authoredTop);       // recovered, not raw
                assert(sw.shiftPx == wrappedTop - authoredTop);
                top = sw.authoredTopPx + sw.shiftPx;
                prevShift96 = sw.shift96;
                assert(top == wrappedTop);
            }
            // --- 3. the strip fits one row again: the page comes BACK ---------
            {
                StripShift sb = stripShiftFor(top, prevShift96, authoredTop, sc.dpi);
                assert(sb.shiftPx == 0);
                assert(sb.shift96 == 0);
                top = sb.authoredTopPx + sb.shiftPx;
                prevShift96 = sb.shift96;
                assert(top == oneRowTop);                      // cycle N == cycle 1
            }
        }

        // --- the accumulating rule the code used must FAIL this -------------
        // (what `g_settingsScroll.perTabStripShift96[t] += shift` did: the input
        // was the already-shifted baseline, so the shrink-back asked for 0 and
        // changed nothing).
        int accTop = authoredTop;
        int accShift = 0;
        for (int cycle = 1; cycle <= 3; ++cycle) {
            accShift += pageTopShiftPx(accTop, wrappedTop);     // +=, on the shifted top
            accTop += pageTopShiftPx(accTop, wrappedTop);
            accShift += pageTopShiftPx(accTop, authoredTop);    // the shrink: 0
            accTop += pageTopShiftPx(accTop, authoredTop);
        }
        assert(accTop == wrappedTop);          // it never came back ...
        assert(accTop != oneRowTop);           // ... which is exactly the defect
    }

    // The DPI record is in 96-dpi px and survives a rescale by construction: a
    // shift applied at 96 dpi must come out as the same physical distance at 150 %.
    const StripShift at96 = stripShiftFor(dpiScalePx(authoredTop96, 96), 0,
                                          dpiScalePx(wrappedTop96, 96), 96);
    assert(at96.shift96 == wrappedTop96 - authoredTop96);
    assert(stripShiftPx(at96.shift96, 144) == dpiScalePx(wrappedTop96 - authoredTop96, 144));
    assert(stripShiftPx(at96.shift96, 96) == at96.shiftPx);
}

void testPageChildWidthIsClampedToThePage() {
    struct Case { int scale; int clientW; int x; int authoredW; };
    // The authored geometry of the rows the audit flagged, plus the widest label
    // rows: 496 px at x=30 (a group box), 480 px at x=24 (a big edit), 720 px at
    // x=42 (a full-width row of the keyboard tab).
    const Case cases[] = {
        {100, kClientW96, 30, 496}, {100, kClientW96, 24, 480}, {100, kClientW96, 42, 720},
        {125, 641, 30, 496},        {125, 641, 24, 480},        {125, 641, 42, 720},
        {150, 919, 30, 496},        {150, 919, 24, 480},        {150, 919, 42, 720},
    };
    for (const Case& c : cases) {
        const int limit = c.clientW - 12 * c.scale / 100;      // the tab's right edge
        const int minW = 80 * c.scale / 100;
        const Rect in{c.x * c.scale / 100, 0, c.authoredW * c.scale / 100, 40};
        const Rect out = clampPageChildWidth(in, limit, minW);
        assert(out.w >= minW);                                  // never degenerate
        assert(out.x == in.x);                                  // the left edge is kept
        assert(out.right() <= limit);                           // inside the page
        if (in.right() <= limit) {
            assert(out.w == in.w);                              // idempotent when it fits
        } else {
            // The case is not vacuous: this is the arithmetic the audit measured
            // at 125 % (620 px of group box starting at x=30 in a 641 px client),
            // so the bound really is violated before the clamp runs.
            assert(out.w < in.w);
        }
        // Re-clamping an already-clamped row changes nothing: the solver may run
        // many times per session and a ratchet would eat every row pixel by pixel.
        const Rect again = clampPageChildWidth(out, limit, minW);
        assert(again.w == out.w && again.x == out.x);
    }
    // A child whose left edge is already past the limit keeps its minimum width
    // instead of collapsing to a negative one (a control of width <= 0 disappears
    // from the layout entirely — worse than the overshoot it was clamped for).
    const Rect past = clampPageChildWidth(Rect{700, 0, 200, 30}, 640, 80);
    assert(past.w == 80);
    // No limit (0) is "no information": the row is returned untouched.
    const Rect untouched = clampPageChildWidth(Rect{10, 0, 500, 30}, 0, 80);
    assert(untouched.w == 500);
}

void testPageTopShiftKeepsContentBelowTheTabStrip() {
    // The CI probe measured the display rectangle at y=114 (two rows of tabs)
    // while the authored page starts at y=100 (group boxes) / 110 (labels).
    assert(pageTopShiftPx(100, 114) == 14);
    assert(pageTopShiftPx(110, 114) == 4);
    // One-row strip: the authored tops were solved for exactly this, no shift.
    assert(pageTopShiftPx(88, 88) == 0);
    assert(pageTopShiftPx(100, 92) == 0);
    // Idempotent: content already at/below the display rectangle never moves up.
    assert(pageTopShiftPx(114, 114) == 0);
    assert(pageTopShiftPx(200, 114) == 0);
}

// v1.3.0-beta8 (bug BS-12): the scroll path may MOVE a page child, never
// RESIZE it. The app re-applied the baseline size on every step, which threw
// away the height a row had grown at runtime — the CI probe measured a row
// going 188px -> 168px at offset 7 of 7 (tab 6 @150 %). Whatever the offset,
// the model must hand back the SOLVED size.
void testScrollModelNeverResizesAChild() {
    const Rect solved{40, 200, 300, 188};
    const Rect viewport{16, 114, 510, 500};
    for (int offset = 0; offset <= 600; offset += 37) {
        const ScrolledChild c = scrollChildRect(solved, offset, viewport);
        assert(c.rect.w == solved.w);
        assert(c.rect.h == solved.h);
        assert(c.clip.w <= solved.w && c.clip.h <= solved.h);
    }
    std::cout << "  [PASS] BS-12: scrolling moves a child and never resizes it\n";
}

// v1.3.0-beta8 (bug BS-17): A REFLOW WHILE THE PAGE IS SCROLLED MUST NOT MOVE
// THE LAYOUT.
//
// This models the app's loop with the real model functions: solve (offset 0) ->
// scroll by `offset` -> a live row needs more room -> re-solve -> restore the
// offset. What changed in the fix is the solver's INPUT: the pre-fix code fed it
// the live rectangle (the render, already moved up by the offset), the fix feeds
// it the baseline (the layout, at offset 0).
void testReflowWhileScrolledKeepsTheLayout() {
    const int pageTop = 100;
    const int offset = 40;
    // Two rows of one page, both below the page top (the normal authored gap).
    const ok::layout::Rect baseTop{20, 200, 400, 28};
    const ok::layout::Rect baseDeep{20, 600, 400, 28};
    // What the user sees after scrolling: both rows are `offset` px higher.
    const ok::layout::Rect liveTop{20, baseTop.y - offset, 400, 28};
    const ok::layout::Rect liveDeep{20, baseDeep.y - offset, 400, 28};

    // --- pre-fix: the live rectangle is the solver's input ---
    const int shiftFromLive = ok::layout::pageTopShiftPx(liveTop.y, pageTop);
    assert(shiftFromLive == 0);            // the live row is still below the top
    std::vector<ok::layout::ControlSpec> liveSpecs;
    for (const ok::layout::Rect& r : {liveTop, liveDeep}) {
        ok::layout::ControlSpec c;
        c.id = static_cast<int>(liveSpecs.size()) + 1;
        c.rect = r;
        c.tab = 0;
        c.growable = true;
        c.requiredHeight = 28;             // the text still fits: no real growth
        liveSpecs.push_back(c);
    }
    const ok::layout::LayoutPlan livePlan =
        ok::layout::autoFit(liveSpecs, pageTop, 700);
    assert(livePlan.rects[0].y == 160);    // solved at the live position
    // The solve writes that plan as the new BASELINE and then restores the scroll
    // offset, which subtracts the offset a SECOND time:
    const int renderedTopAfterReflow = livePlan.rects[0].y - offset;
    assert(renderedTopAfterReflow == 120); // was 160 on screen: moved UP by 40...
    assert(renderedTopAfterReflow == liveTop.y - offset);   // ...the offset again
    // ...and the page's depth -- the number the scroll range is built from --
    // shrank by the same amount, so repeating this empties the scrollbar:
    const int depthBefore = baseDeep.bottom();
    const int depthAfter = livePlan.rects[1].y - offset + livePlan.rects[1].h;
    assert(depthBefore - depthAfter == 2 * offset);
    assert(ok::layout::scrollMetrics(400, depthBefore - pageTop).enabled);
    assert(!ok::layout::scrollMetrics(400, depthAfter - pageTop).enabled ==
           (depthAfter - pageTop <= 400));

    // --- the fix: the baseline is the input, the offset is render-only ---
    const ok::layout::Rect startTop = ok::layout::solverInputRect(liveTop, offset);
    assert(startTop.y == baseTop.y);       // 200: the scroll is undone
    std::vector<ok::layout::ControlSpec> baseSpecs;
    for (const ok::layout::Rect& r : {startTop, ok::layout::solverInputRect(liveDeep, offset)}) {
        ok::layout::ControlSpec c;
        c.id = static_cast<int>(baseSpecs.size()) + 1;
        c.rect = r;
        c.tab = 0;
        c.growable = true;
        c.requiredHeight = 28;
        baseSpecs.push_back(c);
    }
    const ok::layout::LayoutPlan basePlan = ok::layout::autoFit(baseSpecs, pageTop, 700);
    assert(basePlan.rects[0].y == baseTop.y);         // the layout is untouched
    assert(basePlan.rects[1].y == baseDeep.y);
    // The render still applies the offset, so the user sees exactly what they
    // saw before the reflow: nothing moves.
    assert(basePlan.rects[0].y - offset == liveTop.y);
    assert(basePlan.rects[1].y - offset == liveDeep.y);

    // A row that really did grow still grows, still only downwards, and the
    // rows below it keep their distance.
    baseSpecs[0].requiredHeight = 60;
    const ok::layout::LayoutPlan grown = ok::layout::autoFit(baseSpecs, pageTop, 700);
    assert(grown.rects[0].y == baseTop.y && grown.rects[0].h == 60);
    assert(grown.rects[1].y == baseDeep.y + (60 - 28));
    std::cout << "  [PASS] BS-17: a reflow while scrolled keeps the layout\n";
}


// v1.3.0-beta8 (bug BS-12): a runtime text change that needs more room is a
// REFLOW request, not a local resize: the controls below the row have to move
// with it (and the scroll range has to grow), or the text runs under them.
void testRuntimeGrowthRequestsAReflow() {
    assert(ok::layout::rowNeedsReflow(188, 216));   // two more lines
    assert(ok::layout::rowNeedsReflow(28, 31));
    assert(!ok::layout::rowNeedsReflow(188, 188));  // the same text
    assert(!ok::layout::rowNeedsReflow(188, 190));  // 2 px: measurement noise
    assert(!ok::layout::rowNeedsReflow(188, 170));  // shorter: never shrink
    // v1.3.0-beta8 (bug BS-16d): the reflow request is deduped by REQUIRED
    // HEIGHT. A row whose text changes every tick but whose need does not must
    // not re-solve the dialog; a strictly taller need must.
    assert(ok::layout::shouldRequestReflow(0, 40));     // first ask
    assert(!ok::layout::shouldRequestReflow(40, 40));   // the solver granted it
    assert(!ok::layout::shouldRequestReflow(40, 41));   // noise: 2 px tolerance
    assert(!ok::layout::shouldRequestReflow(40, 42));   // still noise
    assert(ok::layout::shouldRequestReflow(40, 43));    // a real extra need
    assert(ok::layout::shouldRequestReflow(40, 60));    // one more line
    assert(!ok::layout::shouldRequestReflow(60, 40));   // never shrink
    // ...and the reflow it triggers keeps every offset reachable (BS-01).
    const ScrollMetrics before = ok::layout::scrollMetrics(500, 0);
    const ScrollMetrics after = ok::layout::scrollMetrics(500, 28);
    assert(before.maxTravelPx == 0);
    assert(after.maxTravelPx == 28);
    assert(after.pagePx == 500);
    std::cout << "  [PASS] BS-12: runtime growth asks for a reflow, and the"
                 " reflow grows the travel\n";
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running Dialog Layout Solver Suite ===\n";
    testAuthoredLayoutReproducesTheBug();
    testAutoFitGrowsLabelsAndReflows();
    testAutoFitIsIdempotentAndMonotone();
    testPlanTabsNeverClipsNineHeaders();
    testGrowWindowClampsToWorkArea();
    // v1.3.0-beta5 (bug B1): the real-Windows regression battery.
    testAlwaysVisibleChromeNeverDrivesGrowth();
    testRefitWindowAppliesPartialGrowthAndScroll();
    testRefitWindowGrowsFullyWhenItFits();
    testRefitWindowShrinksOversizedWindowToScroll();
    testFitRectToWorkArea();
    testScrollChildRectClipsAtViewportEdges();
    // v1.3.0-beta8 (bug BS-01): the scrollbar must be enabled exactly when the
    // content overflows — the beta5..beta7 model disabled it in every case the
    // dialog actually produces.
    testScrollMetricsEnableTheBarExactlyWhenContentOverflows();
    testScrollOffsetIsAlwaysReachableAtBothEnds();
    testDpiSweepEveryControlInsideOrScrollable();
    // v1.3.0-beta8 (bug BS-09): the bottom chrome row keeps its X.
    testBottomRowMovesDownOnly();
    // v1.3.0-beta8 (bug BS-10): the page starts below the tab strip, always.
    testPageTopShiftKeepsContentBelowTheTabStrip();
    // v1.3.0-beta8fix1 (bug BS-20): the page's own width is a hard bound.
    testPageChildWidthIsClampedToThePage();
    // v1.3.0-beta8fix1 (bug BS-21): the strip shift is replaced, never accumulated.
    testStripShiftReturnsToAuthoredOnShrink();
    // v1.3.0-beta8fix1 (bug BS-22): a solve is recomputed, not accumulated.
    testReflowIsRecomputedFromAuthored();
    // v1.3.0-beta8 (bug BS-12): scrolling moves, never resizes; runtime text
    // growth is a reflow request, not a local resize.
    testScrollModelNeverResizesAChild();
    testRuntimeGrowthRequestsAReflow();
    // v1.3.0-beta8 (bug BS-17): a reflow while the page is scrolled must not
    // move the layout (the solver starts from the baseline, not from the render).
    testReflowWhileScrolledKeepsTheLayout();
    std::cout << "=== ALL DIALOG LAYOUT TESTS PASSED ===\n";
    return 0;
}
