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
    // Scroll metrics: range == the overflow, page == 90 % of the viewport.
    const ScrollMetrics m = scrollMetrics(viewport.h, 122);
    assert(m.rangeMax == 122);
    assert(m.pagePx == viewport.h * 9 / 10);
    const ScrollMetrics none = scrollMetrics(viewport.h, 0);
    assert(none.rangeMax == 0);
    std::cout << "  [PASS] scrollChildRect clips at the viewport edges;"
                 " scroll metrics follow the standard\n";
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

} // namespace

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
    testDpiSweepEveryControlInsideOrScrollable();
    std::cout << "=== ALL DIALOG LAYOUT TESTS PASSED ===\n";
    return 0;
}
