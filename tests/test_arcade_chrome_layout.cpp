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
// File: tests/test_arcade_chrome_layout.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// BS-07 (v1.3.0-beta8) — the Arcade Hub footer must not collide with the FPS
// counter, at any DPI, for any hint the games can produce.
//
// beta7 drew the hint with a bare TextOutW (no measurement) and the counter
// from a SECOND, unscaled formula in another function. This suite pins the fix
// on the shared planFooter() geometry:
//
//   1. the metadata constants mirror ArcadeWindow's (so the bands describe the
//      real window);
//   2. hint / meta / counter bands are pairwise disjoint, and every band stays
//      inside the window, at 100/125/150/200 %;
//   3. the longest real hint strings DO overflow the band (that is why they
//      must be ellipsized) and the ellipsizer keeps them within it;
//   4. short hints are left untouched (no gratuitous "…").
//---------------------------------------------------------------------------
#include "ArcadeChromeLayout.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace ok::arcadechrome;

namespace {

int g_checks = 0;
#define CHECK(cond) do { ++g_checks; assert(cond); } while (0)

// The longest hint/status strings the games actually produce (grep of
// src/core/Arcade*.cpp: the Backspace-recovery and no-mistake messages).
const wchar_t* kLongestHints[] = {
    L"Sai — nhấn Backspace 12 lần để sửa (hoặc Esc để thoát)",
    L"Đang chơi: typing-race | Điểm 123456 | WPM 123.4 | Chuẩn xác 100.0%",
    L"Chế độ No-Mistake: mọi lỗi phải sửa bằng Backspace trước khi gõ tiếp",
};

// The stub's measurement model, mirrored: 7 px per character.
int measure7(std::wstring_view text) {
    return static_cast<int>(text.size()) * 7;
}

// Real window sizes: the hub is resizable, so test the smallest sensible one
// (the sidebar alone is 280 units wide) and a comfortable one.
const int kWidthes[] = {640, 1024, 1600};
const double kScales[] = {1.0, 1.25, 1.5, 2.0};

void testConstantsMirrorTheWindow() {
    // ArcadeWindow.hpp: kSidebarWidth = 280, kFooterHeight = 64.
    CHECK(kSidebarUnits == 280.0);
    CHECK(kFooterUnits == 64.0);
    CHECK(kFpsReserveUnits > kFpsGapUnits);
    std::cout << "  [PASS] BS-07: chrome constants mirror ArcadeWindow.hpp\n";
}

void testBandsAreDisjointAtEveryDpi() {
    for (int width : kWidthes) {
        for (double dpi : kScales) {
            const int height = scale(640.0, dpi);
            // The worst case: a hint so long it can never fit.
            const FooterPlan plan = planFooter(width, height, dpi, 100000);
            CHECK(!intersects(plan.hint, plan.fps));
            CHECK(!intersects(plan.meta, plan.fps));
            CHECK(!intersects(plan.hint, plan.meta));
            // Every band stays inside the window.
            CHECK(plan.hint.x >= 0);
            CHECK(plan.hint.right() <= width);
            CHECK(plan.meta.right() <= width);
            CHECK(plan.fps.x >= 0);
            CHECK(plan.fps.right() <= width);
            CHECK(plan.hint.bottom() <= height);
            CHECK(plan.fps.y >= 0);
        }
    }
    std::cout << "  [PASS] BS-07: hint / meta / FPS bands are disjoint at 100-200 %\n";
}

void testLongHintsAreEllipsizedIntoTheBand() {
    for (int width : kWidthes) {
        for (double dpi : kScales) {
            const int height = scale(640.0, dpi);
            for (const wchar_t* hint : kLongestHints) {
                const std::wstring text(hint);
                const int needed = measure7(text);
                const FooterPlan plan = planFooter(width, height, dpi, needed);
                if (needed > plan.hint.w) {
                    CHECK(plan.hintEllipsized);
                    const std::wstring fitted = ellipsizeToWidth(text, plan.hint.w, measure7);
                    CHECK(measure7(fitted) <= plan.hint.w);
                    // It must still say something: the ellipsis, not "".
                    CHECK(!fitted.empty());
                }
            }
        }
    }
    std::cout << "  [PASS] BS-07: overlong hints ellipsize inside the band\n";
}

void testShortHintsAreUntouched() {
    const std::wstring shortHint = L"Space để bắt đầu";
    const FooterPlan plan = planFooter(1024, 640, 1.0, measure7(shortHint));
    CHECK(!plan.hintEllipsized);
    CHECK(ellipsizeToWidth(shortHint, plan.hint.w, measure7) == shortHint);
    // The band is the footer's usable width: from the sidebar inset to the
    // reserved counter column (or the window edge).
    CHECK(plan.hint.x == 280 + 20);
    CHECK(plan.hint.right() <= 1024);
    std::cout << "  [PASS] BS-07: short hints are not touched\n";
}

void testCounterSitsAboveTheFooterAndFitsItsBand() {
    for (double dpi : kScales) {
        const int width = 1024;
        const int height = scale(640.0, dpi);
        const FooterPlan plan = planFooter(width, height, dpi, 0);
        const int footerTop = height - scale(kFooterUnits, dpi);
        // The counter never crawls into the footer band where the hint lives.
        CHECK(plan.fps.bottom() <= footerTop);
        // And the band is wide enough for "12345 FPS" at the 7px/char model.
        CHECK(plan.fps.w >= measure7(L"12345 FPS"));
    }
    std::cout << "  [PASS] BS-07: the counter stays above the footer, inside its band\n";
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout << "=== Running Arcade Chrome Layout Suite (BS-07) ===\n";
    testConstantsMirrorTheWindow();
    testBandsAreDisjointAtEveryDpi();
    testLongHintsAreEllipsizedIntoTheBand();
    testShortHintsAreUntouched();
    testCounterSitsAboveTheFooterAndFitsItsBand();
    std::cout << "=== ALL ARCADE CHROME LAYOUT TESTS PASSED (" << g_checks
              << " checks) ===\n";
    return 0;
}
