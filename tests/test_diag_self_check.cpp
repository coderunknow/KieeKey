//============================================================================
// KieeKey - A modified version based on OpenKey
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
// File: tests/test_diag_self_check.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// The layout SELF-CHECK the shipped app runs on itself (v1.3.0-beta8, CA-06).
//
// The user's report — text overlapping, and scrolling scrambling it — survived
// CI because CI's font and DPI are not the user's. src/app/DiagSelfCheck.hpp
// turns "does it look wrong?" into rectangle arithmetic that the app performs
// on its own live dialog, so the next report carries the numbers.
//
// This suite pins the four judgements and the report text:
//   1. a clean plan reports NOTHING (no false alarms: the check runs on every
//      report export, for every user);
//   2. two painted rectangles that intersect are reported, with the overlap
//      size, and a group box containing another control is NOT an overlap;
//   3. text taller than the box it is painted in is reported as `clipped`;
//   4. a control wider than the page, or below it even at full scroll travel,
//      is reported as `cut` / `unreachable`;
//   5. the section reads in Vietnamese, counts what it measured, and never
//      prints more than `maxListed` findings (a report stays mailable).
//
// Run: tests/run_all_tests.sh --quick   (or compile this file directly)
//----------------------------------------------------------------------------
#include "DiagSelfCheck.hpp"

#include <cassert>
#include <iostream>
#include <string>

using ok::diagself::Finding;
using ok::diagself::Item;
using ok::diagself::Plan;
using ok::diagself::Rect;
using ok::diagself::TabPage;

namespace {

Item makeItem(int id, int tab, Rect box, Rect rect, bool shown = true,
              int needH = 0, bool group = false) {
    Item it;
    it.id = id;
    it.tab = tab;
    it.box = box;
    it.rect = rect;
    it.shown = shown;
    it.needH = needH;
    it.group = group;
    it.name = "Static";
    return it;
}

std::size_t count(const std::vector<Finding>& f, const std::string& kind) {
    std::size_t n = 0;
    for (const Finding& x : f) { if (x.kind == kind) { ++n; } }
    return n;
}

Plan onePage(int tab, Rect page, int travel = 0) {
    Plan p;
    p.pages.push_back(TabPage{tab, page, travel});
    p.dpi = 96;
    return p;
}

// 1. A layout that is fine must produce an empty report: this check runs on
//    every export, so a false alarm would train the user to ignore it.
void testCleanPlanIsSilent() {
    Plan p = onePage(1, Rect{16, 114, 510, 500});
    p.items.push_back(makeItem(509, 1, Rect{42, 171, 462, 28}, Rect{42, 171, 462, 28}));
    p.items.push_back(makeItem(510, 1, Rect{42, 217, 462, 30}, Rect{42, 217, 462, 30}));
    p.items.push_back(makeItem(600, ok::diagself::kChrome, Rect{24, 971, 360, 45},
                               Rect{24, 971, 360, 45}));
    const std::vector<Finding> f = ok::diagself::findProblems(p);
    assert(f.empty());
    const std::string s = ok::diagself::formatSection(p, f);
    assert(s.find("Kết quả: OK") != std::string::npos);
    assert(s.find("1 tab") != std::string::npos);
    std::cout << "  [PASS] a correct layout reports nothing (0 findings)\n";
}

// 2. The user's symptom, measured: two painted rectangles that intersect.
void testOverlapIsReportedWithSize() {
    Plan p = onePage(3, Rect{16, 114, 510, 500});
    p.items.push_back(makeItem(596, 3, Rect{44, 120, 240, 30}, Rect{44, 120, 240, 30}));
    // ...and a neighbour drawn 6px too high: 120x6 px of ink on top of it.
    p.items.push_back(makeItem(597, 3, Rect{44, 144, 120, 30}, Rect{44, 144, 120, 30}));
    const std::vector<Finding> f = ok::diagself::findProblems(p);
    assert(count(f, "overlap") == 1);
    assert(f[0].text.find("120x6px") != std::string::npos);
    assert(f[0].text.find("#596") != std::string::npos);
    assert(f[0].text.find("#597") != std::string::npos);
    std::cout << "  [PASS] overlap reported with the exact overlap size\n";
}

// 3. A group box CONTAINS its children; that is not an overlap, and reporting
//    it would bury the real findings under ~40 false ones per tab.
void testGroupBoxContainmentIsNotOverlap() {
    Plan p = onePage(4, Rect{16, 114, 510, 500});
    p.items.push_back(makeItem(558, 4, Rect{24, 100, 494, 196}, Rect{24, 100, 494, 196},
                               true, 0, /*group=*/true));
    p.items.push_back(makeItem(559, 4, Rect{44, 120, 240, 30}, Rect{44, 120, 240, 30}));
    const std::vector<Finding> f = ok::diagself::findProblems(p);
    assert(count(f, "overlap") == 0);
    // ...but a HIDDEN control never overlaps anything either (only one tab is
    // painted at a time — every other tab's controls are in the same parent).
    Plan q = onePage(4, Rect{16, 114, 510, 500});
    q.items.push_back(makeItem(559, 4, Rect{44, 120, 240, 30}, Rect{44, 120, 240, 30}));
    q.items.push_back(makeItem(560, 4, Rect{44, 120, 240, 30}, Rect{44, 120, 240, 30},
                               /*shown=*/false));
    assert(ok::diagself::findProblems(q).empty());
    // ...and containment by GEOMETRY alone is enough: even with the group flag
    // unknown (a container that is not a BS_GROUPBOX button), a rectangle that
    // fully encloses a visible child is not an overlap.
    Plan r = onePage(4, Rect{16, 114, 510, 500});
    r.items.push_back(makeItem(630, 4, Rect{24, 100, 494, 300}, Rect{24, 100, 494, 300}));
    r.items.push_back(makeItem(631, 4, Rect{44, 120, 240, 30}, Rect{44, 120, 240, 30}));
    assert(ok::diagself::findProblems(r).empty());
    std::cout << "  [PASS] group boxes contain, hidden tabs do not overlap\n";
}

// 4. Text that cannot fit: the "đè chữ" report from the user's screen.
void testClippedTextIsReported() {
    Plan p = onePage(2, Rect{16, 114, 510, 500});
    // 28px of box, 56px of wrapped text at that width — the CI probe measured
    // exactly this shape (06/09: "wraps to 56px (app solver says 28)").
    p.items.push_back(makeItem(541, 2, Rect{42, 171, 462, 28}, Rect{42, 171, 462, 28},
                               true, 56));
    const std::vector<Finding> f = ok::diagself::findProblems(p);
    assert(count(f, "clipped") == 1);
    assert(f[0].text.find("56px") != std::string::npos);
    assert(f[0].text.find("28px") != std::string::npos);
    // One pixel of rounding slack must NOT be reported.
    Plan q = onePage(2, Rect{16, 114, 510, 500});
    q.items.push_back(makeItem(541, 2, Rect{42, 171, 462, 28}, Rect{42, 171, 462, 28},
                               true, 29));
    assert(ok::diagself::findProblems(q).empty());
    // ...and a child the app clipped ON PURPOSE because it is scrolled out of
    // the page is scrolling, not a defect (its painted rect is short by design).
    // (box 500..528 straddles the page bottom 514: the app clips it to 500..514)
    Plan r = onePage(2, Rect{16, 114, 510, 400});
    r.items.push_back(makeItem(542, 2, Rect{42, 500, 462, 28}, Rect{42, 500, 462, 14},
                               true, 56));
    assert(ok::diagself::findProblems(r).empty());
    // ...and a control the page shows WHOLE that is nevertheless clipped by a
    // stale region paints background only — the "grey band where the label
    // should be" symptom, reported instead of guessed at.
    Plan v = onePage(2, Rect{16, 114, 510, 500});
    Item stale = makeItem(543, 2, Rect{42, 240, 462, 28}, Rect{42, 240, 462, 28});
    stale.hasRgn = true;
    stale.rgn = Rect{0, 0, 120, 28};
    v.items.push_back(stale);
    const std::vector<Finding> fv = ok::diagself::findProblems(v);
    assert(count(fv, "region") == 1);
    assert(fv[0].text.find("#543") != std::string::npos);
    // A region that matches the box (the normal, unclipped case) is silent.
    Plan w = onePage(2, Rect{16, 114, 510, 500});
    Item okc = makeItem(544, 2, Rect{42, 240, 462, 28}, Rect{42, 240, 462, 28});
    okc.hasRgn = true;
    okc.rgn = Rect{0, 0, 462, 28};
    w.items.push_back(okc);
    assert(ok::diagself::findProblems(w).empty());
    std::cout << "  [PASS] clipped text reported, 1px rounding tolerated,\n"
                 "         scroll-clipped children not mistaken for it,\n"
                 "         a stale region on a visible control is reported\n";
}

// 5. Horizontally cut and vertically unreachable: the two ways content can sit
//    outside what the tab region lets the user see or scroll to.
void testCutAndUnreachable() {
    Plan p = onePage(5, Rect{16, 114, 510, 400}, /*travel=*/120);
    // 60px wider than the page's right edge (526), so the region clips it.
    p.items.push_back(makeItem(589, 5, Rect{66, 200, 520, 40}, Rect{66, 200, 460, 40}));
    // Reaches the bottom: 400 - 120 = 280 → visible at full travel.
    p.items.push_back(makeItem(590, 5, Rect{66, 260, 450, 30}, Rect{66, 260, 450, 30}));
    // 26px below what the whole 120px travel can bring into view
    // (page bottom 514 + travel 120 = 634).
    p.items.push_back(makeItem(591, 5, Rect{66, 660, 450, 30}, Rect{66, 660, 450, 30}));
    const std::vector<Finding> f = ok::diagself::findProblems(p);
    assert(count(f, "cut") == 1);
    assert(f[0].text.find("60px") != std::string::npos);
    assert(count(f, "unreachable") == 1);
    assert(f[1].text.find("#591") != std::string::npos);
    std::cout << "  [PASS] cut width + unreachable depth reported, reachable kept\n";
}

// 6. The section itself: one header, the counts, and a bounded list.
void testReportSectionIsReadableAndBounded() {
    Plan p = onePage(8, Rect{16, 114, 510, 500});
    for (int i = 0; i < 20; ++i) {
        p.items.push_back(makeItem(600 + i, 8,
                                   Rect{42, 100 + i * 20, 462, 30},
                                   Rect{42, 100 + i * 20, 462, 30}));
    }
    const std::vector<Finding> f = ok::diagself::findProblems(p);
    assert(f.size() == 19);              // 20 stacked rows: 19 pair contacts
    const std::string s = ok::diagself::formatSection(p, f);
    assert(s.find("=== Tự kiểm tra bố cục") != std::string::npos);
    assert(s.find("CÓ LỖI — 19 mục") != std::string::npos);
    assert(s.find("… và 7 mục nữa.") != std::string::npos);
    assert(s.find("20 điều khiển") != std::string::npos);
    std::cout << "  [PASS] the section is counted, listed and bounded\n";
}

// 7. v1.3.0-beta8 (CA-06): the window/scroll facts — the two numbers every
// layout bug begins with — are printed when the caller measured them, and are
// not invented when it did not (the pure suite and the probe pass no client).
void testWindowFactsAppearOnlyWhenKnown() {
    Plan p = onePage(2, Rect{16, 114, 510, 500});
    const std::string bare = ok::diagself::formatSection(p, {});
    assert(bare.find("Cửa sổ: client") == std::string::npos);
    p.client = Rect{0, 0, 486, 689};
    p.scrollEnabled = true;
    p.scrollRange = 64;
    const std::string s = ok::diagself::formatSection(p, {});
    assert(s.find("Cửa sổ: client") != std::string::npos);
    assert(s.find("486x689") != std::string::npos);
    assert(s.find("thanh cuộn BẬT") != std::string::npos);
    assert(s.find("tầm cuộn tab này 64 px") != std::string::npos);
    assert(s.find("tab 2: vùng trang") != std::string::npos);
    std::cout << "  [PASS] window + scroll facts printed only when measured\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running Layout Self-Check Suite ===\n";
    testCleanPlanIsSilent();
    testOverlapIsReportedWithSize();
    testGroupBoxContainmentIsNotOverlap();
    testClippedTextIsReported();
    testCutAndUnreachable();
    testReportSectionIsReadableAndBounded();
    testWindowFactsAppearOnlyWhenKnown();
    std::cout << "=== ALL LAYOUT SELF-CHECK TESTS PASSED ===\n";
    return 0;
}
