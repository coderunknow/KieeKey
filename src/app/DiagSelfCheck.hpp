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
// File: src/app/DiagSelfCheck.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// v1.3.0-beta8 (CA-06) — the layout SELF-CHECK inside the shipped app.
//
// WHY
//   beta8's CI probe measures the real dialog on a build machine at 96 and
//   150 %, and the user's report ("chữ đè lên nhau, cuộn thì loạn") survived
//   that: their machine has a DPI, a font scale and a text scale that CI does
//   not have. Asking them for a screenshot answers "does it look wrong"; it
//   cannot answer "which rectangle overlaps which, by how many pixels, on
//   which tab, at which DPI".
//
//   So the app now measures itself. "Xuất báo cáo" (and the report pane) runs
//   this check over all nine tabs of the OPEN settings dialog and appends the
//   result to the report the user sends us. The same three questions the CI
//   probe asks are asked here, with the same definitions:
//
//     overlap  — two visible controls whose PAINTED rectangles intersect
//                (group boxes excluded: containing is not overlapping)
//     clipped  — a static/checkbox whose measured wrapped text needs more
//                height than its (clipped) box has, i.e. text the user cannot
//                read — the "đè chữ" report, seen without eyes
//     cut      — a control that is wider than the reachable page, so the tab
//                region clips its right edge
//     unreachable — a control that even at the end of the tab's scroll travel
//                sits below the page: content the user can never scroll to
//
//   PORTABLE ON PURPOSE: rectangles in, text out, no <windows.h> — the whole
//   decision layer is pinned by tests/test_diag_self_check.cpp on every
//   platform (the same reason DiagReportText.hpp is portable, DS-01).
//---------------------------------------------------------------------------
#ifndef KIEEKEY_APP_DIAG_SELF_CHECK_HPP
#define KIEEKEY_APP_DIAG_SELF_CHECK_HPP

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace ok::diagself {

// A page child of the settings tab control, or an always-visible chrome
// control (the tab strip, the four bottom buttons). `kChrome` items are never
// overlap-checked against page items: they live in a band of their own.
inline constexpr int kChrome = -1;

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] int right() const { return x + w; }
    [[nodiscard]] int bottom() const { return y + h; }
    [[nodiscard]] bool intersects(const Rect& o) const {
        return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
    }
    [[nodiscard]] Rect clippedTo(const Rect& outer) const {
        const int l = (x > outer.x) ? x : outer.x;
        const int t = (y > outer.y) ? y : outer.y;
        const int r = (right() < outer.right()) ? right() : outer.right();
        const int b = (bottom() < outer.bottom()) ? bottom() : outer.bottom();
        return Rect{l, t, (r > l) ? r - l : 0, (b > t) ? b - t : 0};
    }
};

struct Item {
    int id = 0;
    int tab = kChrome;
    Rect box{};       // the window rectangle, parent coordinates
    Rect rect{};      // what is actually painted: box ∩ region ∩ page
    Rect rgn{};       // the window REGION box (child-local), 0x0 = none set
    bool hasRgn = false;
    int needH = 0;    // measured wrapped-text height, 0 = not a text control
    bool shown = false;
    bool group = false;     // BS_GROUPBOX frame: may contain, never overlaps
    std::string name;       // class + the start of the control's own text
};

struct TabPage {
    int tab = 0;
    Rect rect{};            // the reachable page: tab display rect ∩ client
    int travelPx = 0;       // how far this tab can scroll (0 = no scrollbar)
};

struct Plan {
    std::vector<Item> items;
    std::vector<TabPage> pages;
    int dpi = 96;
    int controls = 0;       // what the check looked at (== items.size())
    int checks = 0;         // how many comparisons were made
    // v1.3.0-beta8 (CA-06): the window itself, so a report from a machine we
    // cannot see still answers the two questions every layout bug starts with —
    // how much room did the window actually get, and is the page scrolling?
    Rect client{};          // dialog client area (0 width => not reported)
    bool scrollEnabled = false;
    int  scrollRange = 0;   // px the current tab can travel
};

struct Finding {
    std::string kind;       // overlap | clipped | cut | unreachable | region
    std::string text;       // one Vietnamese line naming the control + numbers
};

// `outer` completely encloses `inner` (a group box around its children, a
// frame behind a row): the layout working as designed, never an overlap.
[[nodiscard]] inline bool encloses(const Rect& outer, const Rect& inner) {
    return outer.x <= inner.x && outer.y <= inner.y &&
           outer.right() >= inner.right() && outer.bottom() >= inner.bottom();
}

namespace detail {

inline std::string rectText(const Rect& r) {
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%d,%d %dx%d", r.x, r.y, r.w, r.h);
    return buf;
}

inline std::string trimText(const std::string& in, std::size_t maxLen) {
    std::string out;
    out.reserve(maxLen + 4);
    std::size_t chars = 0;
    for (const char c : in) {
        if (c == '\r' || c == '\n' || c == '\t') {
            if (!out.empty() && out.back() != ' ') { out.push_back(' '); }
            continue;
        }
        if (chars >= maxLen) { out += "…"; break; }
        out.push_back(c);
        ++chars;
    }
    while (!out.empty() && out.back() == ' ') { out.pop_back(); }
    return out;
}

inline std::string label(const Item& it) {
    std::string s = it.name.empty() ? std::string("mục") : it.name;
    char idbuf[24]{};
    std::snprintf(idbuf, sizeof(idbuf), " #%d", it.id);
    return s + idbuf;
}

}  // namespace detail

// The whole check: pure rectangle/text arithmetic over one snapshot.
[[nodiscard]] inline std::vector<Finding> findProblems(Plan& plan) {
    std::vector<Finding> out;
    plan.controls = static_cast<int>(plan.items.size());

    for (const TabPage& page : plan.pages) {
        for (std::size_t i = 0; i < plan.items.size(); ++i) {
            const Item& a = plan.items[i];
            if (a.tab != page.tab || !a.shown) { continue; }

            // A control the page shows WHOLE: not scrolled out of the viewport.
            // Everything below is judged only for such controls (see clipped).
            const bool fullyVisible = a.box.y >= page.rect.y &&
                                      a.box.bottom() <= page.rect.bottom() + 1;

            // -- clipped text: the box cannot hold the wrapped text ---------
            //
            // A child that is scrolled half out of the viewport is resized to
            // the clip by the app on purpose (scrollChildRect), and calling
            // that "text does not fit" would flood the report with the
            // scrolling design working as intended — hence fullyVisible.
            if (a.needH > 0 && fullyVisible && a.rect.h > 0 && a.rect.h + 1 < a.needH) {
                ++plan.checks;
                out.push_back({"clipped",
                    "chữ cần " + std::to_string(a.needH) + "px cao nhưng ô chỉ " +
                    std::to_string(a.rect.h) + "px — " + detail::label(a) + " tại " +
                    detail::rectText(a.box) + " (có thể bị cắt/đè)"});
            }

            // -- region: ink cut away with no scroll to blame ----------------
            //
            // The app clips scrolled children with SetWindowRgn. A region on a
            // control the page shows WHOLE means the clip is stale — the
            // control paints its background and nothing else, which is what a
            // band of empty grey where a label should be looks like.
            if (a.hasRgn && fullyVisible && a.rect.w > 0 && a.rect.h > 0 &&
                (a.rgn.w + 1 < a.box.w || a.rgn.h + 1 < a.box.h)) {
                ++plan.checks;
                out.push_back({"region",
                    "bị cắt bởi vùng vẽ cũ: vùng " + detail::rectText(a.rgn) +
                    " nhỏ hơn ô " + detail::rectText(a.box) + " — " +
                    detail::label(a)});
            }

            // -- cut: wider than the page → the tab region clips the right ---
            if (a.box.x + a.box.w > page.rect.right() + 8) {
                ++plan.checks;
                out.push_back({"cut",
                    "rộng hơn vùng tab " + std::to_string(a.box.x + a.box.w -
                        page.rect.right()) + "px ở bên phải — " + detail::label(a) +
                    " tại " + detail::rectText(a.box) + " (vùng tab " +
                    detail::rectText(page.rect) + ")"});
            }

            // -- unreachable: below the page even at full travel -------------
            if (a.box.y >= page.rect.bottom() + page.travelPx + 2) {
                ++plan.checks;
                out.push_back({"unreachable",
                    "nằm dưới trang kể cả khi đã cuộn hết " +
                    std::to_string(page.travelPx) + "px — " + detail::label(a) +
                    " tại " + detail::rectText(a.box) + " (trang " +
                    detail::rectText(page.rect) + ")"});
            }

            // -- overlap: painted rectangles of two visible controls ---------
            if (a.group) { continue; }
            for (std::size_t j = i + 1; j < plan.items.size(); ++j) {
                const Item& b = plan.items[j];
                if (b.tab != page.tab || !b.shown || b.group) { continue; }
                if (a.rect.w <= 0 || a.rect.h <= 0 || b.rect.w <= 0 || b.rect.h <= 0) {
                    continue;
                }
                if (!a.rect.intersects(b.rect)) { continue; }
                // Containment is not overlap — a group box around its own
                // children, or a frame behind the row it labels, is correct.
                if (encloses(a.rect, b.rect) || encloses(b.rect, a.rect)) { continue; }
                ++plan.checks;
                Rect hit = a.rect.clippedTo(b.rect);
                out.push_back({"overlap",
                    "đè nhau " + std::to_string(hit.w) + "x" + std::to_string(hit.h) +
                    "px — " + detail::label(a) + " " + detail::rectText(a.box) +
                    " chồng " + detail::label(b) + " " + detail::rectText(b.box)});
            }
        }
    }
    return out;
}

// The report section, in Vietnamese, ready for a user to paste into a mail.
[[nodiscard]] inline std::string formatSection(const Plan& plan,
                                               const std::vector<Finding>& findings,
                                               std::size_t maxListed = 12) {
    std::string s;
    s += "\n=== Tự kiểm tra bố cục (đo trên cửa sổ đang mở) ===\n";
    s += "DPI hiệu dụng: " + std::to_string(plan.dpi) + " (" +
         std::to_string(plan.dpi * 100 / 96) + " %)\n";
    std::size_t shownItems = 0;
    for (const Item& it : plan.items) { if (it.shown) { ++shownItems; } }
    s += "Đã đo: " + std::to_string(plan.pages.size()) + " tab, " +
         std::to_string(plan.controls) + " điều khiển (" +
         std::to_string(shownItems) + " đang hiện), " +
         std::to_string(plan.checks) + " phép so\n";
    if (plan.client.w > 0) {
        s += "Cửa sổ: client " + detail::rectText(plan.client) + "; thanh cuộn " +
             (plan.scrollEnabled ? "BẬT" : "tắt") + " (tầm cuộn tab này " +
             std::to_string(plan.scrollRange) + " px)\n";
        for (const TabPage& p : plan.pages) {
            s += "  tab " + std::to_string(p.tab) + ": vùng trang " + detail::rectText(p.rect) +
                 " + " + std::to_string(p.travelPx) + " px cuộn\n";
        }
    }
    if (findings.empty()) {
        s += "Kết quả: OK — không mục nào đè, cắt hay nằm ngoài tầm với.\n";
        return s;
    }
    s += "Kết quả: CÓ LỖI — " + std::to_string(findings.size()) + " mục:\n";
    const std::size_t listed = (findings.size() < maxListed) ? findings.size() : maxListed;
    for (std::size_t i = 0; i < listed; ++i) {
        s += "  " + std::to_string(i + 1) + ") [" + findings[i].kind + "] " +
             findings[i].text + "\n";
    }
    if (findings.size() > listed) {
        s += "  … và " + std::to_string(findings.size() - listed) + " mục nữa.\n";
    }
    return s;
}

}  // namespace ok::diagself

#endif  // KIEEKEY_APP_DIAG_SELF_CHECK_HPP
