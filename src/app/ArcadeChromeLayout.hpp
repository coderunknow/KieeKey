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
// File: src/app/ArcadeChromeLayout.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// v1.3.0-beta8 (bug BS-07) — the Arcade Hub's footer/overlay geometry.
//
// WHY THIS HEADER IS PORTABLE (and why it exists)
//   drawChrome() drew the footer hint with a bare TextOutW — no measurement —
//   while the FPS counter (presentFrame(), a different function, drawn later)
//   sat at an UNSCALED `width - 76, height - footer - 22`. Two functions, two
//   ideas of the same corner: at 125/150 % the footer text scales and the
//   counter does not, and a long hint ("Sai — nhấn Backspace N lần để sửa")
//   simply ran off its own edge with nothing to stop it.
//
//   Both callers now ask THIS function where their band is, so the hint band
//   and the counter band can never disagree — and because it is pure integer
//   maths with no HWND, tests/test_arcade_chrome_layout.cpp can pin the
//   invariant (pairwise disjoint bands, inside the window) at
//   100/125/150/200 % for the longest hint strings the games produce.
//
//   The counter is right-aligned inside its band, so no measurement of its
//   text is needed here: the band is a fixed reserve, the text is fitted into
//   it at draw time.
//---------------------------------------------------------------------------
#ifndef KIEEKEY_APP_ARCADE_CHROME_LAYOUT_HPP
#define KIEEKEY_APP_ARCADE_CHROME_LAYOUT_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace ok::arcadechrome {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    [[nodiscard]] constexpr int right() const noexcept { return x + w; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + h; }
};

[[nodiscard]] constexpr bool intersects(const Rect& a, const Rect& b) noexcept {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

// The chrome constants live in ArcadeWindow; mirrored here as plain numbers so
// this header stays dependency-free (a test pins that they match).
inline constexpr double kSidebarUnits   = 280.0;
inline constexpr double kFooterUnits    = 64.0;
inline constexpr double kFooterPadUnits = 20.0;   // hint/meta left inset
inline constexpr double kHintTopUnits   = 12.0;   // hint baseline offset
inline constexpr double kMetaTopUnits   = 38.0;   // meta baseline offset
inline constexpr double kHintLineUnits  = 18.0;   // one line of the hint font
inline constexpr double kFpsReserveUnits = 76.0;  // counter band, from the right edge
inline constexpr double kFpsGapUnits    = 8.0;    // clear air around the counter
inline constexpr double kFpsAboveUnits  = 22.0;   // counter sits above the footer
inline constexpr double kEdgeUnits      = 8.0;    // never draw closer than this

struct FooterPlan {
    Rect hint{};             // where the hint text may draw (w = allowed width)
    Rect meta{};             // the stats line, same band rules
    Rect fps{};              // the FPS counter's reserved band
    int  hardRight = 0;      // the last pixel any footer text may touch
    bool hintEllipsized = false;   // measured hint text did not fit
};

[[nodiscard]] inline int scale(double units, double dpiScale) noexcept {
    return static_cast<int>(std::lround(units * dpiScale));
}

// Plan the footer band. `hintTextWidthPx` is the MEASURED width of the hint at
// the current font (0 = nothing drawn); `fpsLineHeightPx` is the counter's line
// height (0 = fall back to the hint line height).
[[nodiscard]] inline FooterPlan planFooter(int widthPx, int heightPx, double dpiScale,
                                           int hintTextWidthPx,
                                           int fpsLineHeightPx = 0) noexcept {
    FooterPlan plan{};
    const int sidebar = scale(kSidebarUnits, dpiScale);
    const int footer  = scale(kFooterUnits, dpiScale);
    const int pad     = scale(kFooterPadUnits, dpiScale);
    const int edge    = scale(kEdgeUnits, dpiScale);
    const int gap     = scale(kFpsGapUnits, dpiScale);
    const int hintH   = scale(kHintLineUnits, dpiScale);
    const int fpsH    = fpsLineHeightPx > 0 ? fpsLineHeightPx : scale(16.0, dpiScale);
    const int fpsTop  = heightPx - footer - scale(kFpsAboveUnits, dpiScale);

    plan.hardRight = std::max(0, widthPx - edge);
    plan.hint = Rect{sidebar + pad, heightPx - footer + scale(kHintTopUnits, dpiScale),
                     std::max(0, plan.hardRight - (sidebar + pad)), hintH};
    plan.meta = Rect{plan.hint.x, heightPx - footer + scale(kMetaTopUnits, dpiScale),
                     plan.hint.w, hintH};
    plan.fps = Rect{std::max(0, widthPx - scale(kFpsReserveUnits, dpiScale)), fpsTop,
                    scale(kFpsReserveUnits, dpiScale) - edge, fpsH};

    // The hint may never enter the counter's column. Today the counter sits
    // ABOVE the footer band, so this only bites when a layout change (or a
    // very short footer) brings the two bands together — which is exactly the
    // case the report could not see and the test pins.
    if (intersects(plan.hint, plan.fps) || (plan.hint.right() > plan.fps.x)) {
        const int allowed = std::max(0, plan.fps.x - gap - plan.hint.x);
        if (plan.hint.y < plan.fps.bottom() && plan.fps.y < plan.hint.bottom()) {
            plan.hint.w = std::min(plan.hint.w, allowed);
            plan.meta.w = plan.hint.w;
        }
    }
    plan.hintEllipsized = hintTextWidthPx > plan.hint.w;
    return plan;
}

//---------------------------------------------------------------------------
// Ellipsize to a band. `measure` returns the pixel width of a string in the
// current font (TextOutW's own measurement in the app, 7 px/char in the stub).
//---------------------------------------------------------------------------
template <class MeasureFn>
[[nodiscard]] inline std::wstring ellipsizeToWidth(std::wstring_view text,
                                                   int maxWidthPx,
                                                   MeasureFn&& measure) {
    if (maxWidthPx <= 0 || text.empty()) { return std::wstring(); }
    if (measure(text) <= maxWidthPx) { return std::wstring(text); }
    constexpr wchar_t kEllipsis = L'…';
    std::wstring out(text);
    while (!out.empty()) {
        out.pop_back();
        // Trim a dangling space so "abc …" never happens.
        while (!out.empty() && out.back() == L' ') { out.pop_back(); }
        std::wstring probe = out;
        probe.push_back(kEllipsis);
        if (measure(probe) <= maxWidthPx) { return probe; }
    }
    return std::wstring(1, kEllipsis);
}

} // namespace ok::arcadechrome

#endif // KIEEKEY_APP_ARCADE_CHROME_LAYOUT_HPP
