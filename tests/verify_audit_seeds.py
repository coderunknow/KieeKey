#!/usr/bin/env python3
# ============================================================================
# KieeKey - A modified version based on OpenKey
#
# Modified work:
#   KieeKey - refactored and completed logic
#   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
#   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# File: tests/verify_audit_seeds.py
# SPDX-License-Identifier: GPL-3.0-or-later
# ============================================================================
"""Seed-verification harness for the static layout audit (v1.3.0-beta8).

WHY THIS EXISTS
    scripts/audit_layout.py gained four new checks in beta8 (CA-01a..d):

        a  text fit at 100/125/150 %               (kind: clip)
        b  controls under a combo's created window (kind: under_combo)
        b2 a combo authored tall / never asking for its drop height
                                                   (kind: combo_window)
        c  group boxes that do not contain theirs  (kind: outside_group)
        d  one-line rows that cannot hold their text (kind: clip)
        e  GetClassNameW compared case-sensitively (kind: class_case)

    A check that has never failed is not evidence. This harness SEEDS one
    violation of every new class into a throwaway copy of the tree and asserts
    that the audit goes RED with the expected finding kind — then leaves the
    real tree untouched. Run it whenever the audit changes:

        python3 tests/verify_audit_seeds.py

Exit 0 = every seeded violation was caught (and the un-seeded tree is green).
"""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent

# (name, file, old snippet, new snippet, expected finding kind)
SEEDS: list[tuple[str, str, str, str, str]] = [
    (
        "CA-01a  one-line static too narrow at every scale",
        "src/app/main.cpp",
        # BS-22c moved the BPM row 8 px down (the combo's real closed height is
        # taller than the authored 25 px), so the anchor follows it.
        'mkCtl(hwnd, L"STATIC", L"Nhịp Rhythm (60-220):",\n'
        '                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(280), S(376), S(160), S(20)',
        'mkCtl(hwnd, L"STATIC", L"Nhịp Rhythm (60-220):",\n'
        '                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(280), S(376), S(100), S(20)',
        "clip",
    ),
    (
        "CA-01b  control under the created (drop-down) window of a later combo",
        "src/app/main.cpp",
        # The code-table combo is created with its 200 px drop-down height; the
        # radios are created BEFORE it. Dropping the combo over them puts their
        # own rects inside its invisible window => under_combo.
        'CBS_DROPDOWNLIST, S(128), S(186), S(210), S(25), reinterpret_cast<HMENU>(IDC_COMBO_CODETABLE));',
        'CBS_DROPDOWNLIST, S(128), S(100), S(210), S(200), reinterpret_cast<HMENU>(IDC_COMBO_CODETABLE));',
        # NOTE: since beta8 a tall combo is itself a finding — this seed also
        # proves the new combo_window contract, and it must keep firing under_combo
        # (the class the beta7 tester actually hit).
        "under_combo",
    ),
    (
        "CA-01c  control below the group box that labels it",
        "src/app/main.cpp",
        # Back to the beta7 height: the steering row then hangs 6 px below the
        # Arcade group (the original BS-04 report).
        'WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(100), S(494), S(356),\n'
        '                  reinterpret_cast<HMENU>(IDC_GRP_ARCADE));',
        'WS_CHILD | WS_VISIBLE | BS_GROUPBOX, S(24), S(100), S(494), S(324),\n'
        '                  reinterpret_cast<HMENU>(IDC_GRP_ARCADE));',
        "outside_group",
    ),
    (
        "CA-01c  group box shorter than its own child",
        "src/app/main.cpp",
        'WS_CHILD | WS_VISIBLE, S(44), S(214), S(450), S(72),\n'
        '                  reinterpret_cast<HMENU>(IDC_STAT_CHAOS_WARN));',
        'WS_CHILD | WS_VISIBLE, S(44), S(214), S(450), S(88),\n'
        '                  reinterpret_cast<HMENU>(IDC_STAT_CHAOS_WARN));',
        "overlap",
    ),
    (
        "CA-01d  one-line label wider than its box",
        "src/app/ChaosLabWindow.cpp",
        'm_impl->flexSend = create(L"BUTTON", L"Gõ chữ Flexing ra app", BS_PUSHBUTTON, 466, 552,\n'
        '                                  150, 26, kIdFlexSend);',
        'm_impl->flexSend = create(L"BUTTON", L"Gõ chữ Flexing ra app", BS_PUSHBUTTON, 466, 552,\n'
        '                                  80, 26, kIdFlexSend);',
        "clip",
    ),
    (
        "CA-01b-2  combo created closed but never asks for its drop-down height",
        "src/app/main.cpp",
        'applyComboDropHeight(perfCombo, S(160));',
        '/* seeded: the drop-height request was dropped */',
        "combo_window",
    ),
    (
        "BS-08   a pinned worst-case note row no longer fits",
        "src/app/main.cpp",
        'WS_CHILD | WS_VISIBLE, S(44), S(462), S(460), S(34),\n'
        '                  reinterpret_cast<HMENU>(IDC_STAT_OUT_NOTE));',
        'WS_CHILD | WS_VISIBLE, S(44), S(462), S(240), S(34),\n'
        '                  reinterpret_cast<HMENU>(IDC_STAT_OUT_NOTE));',
        "pinned_row",
    ),
    (
        "BS-08   a pinned row was renamed away",
        "src/app/main.cpp",
        'reinterpret_cast<HMENU>(IDC_STAT_LIVE_HINT));',
        'reinterpret_cast<HMENU>(IDC_STAT_LIVE_HINT_RENAMED));',
        "pinned_row",
    ),
    (
        "CA-01a  group box that does not contain its own label at 150 %",
        "src/app/ChaosLabWindow.cpp",
        'm_impl->flexLoad = create(L"BUTTON", L"Nạp văn bản", BS_PUSHBUTTON, 466, 520, 100, 26,\n'
        '                                  kIdFlexLoad);',
        'm_impl->flexLoad = create(L"BUTTON", L"Nạp văn bản", BS_PUSHBUTTON, 466, 520, 60, 26,\n'
        '                                  kIdFlexLoad);',
        "clip",
    ),    (
        "CA-01e  GetClassNameW compared against an ALL-CAPS class literal",
        "src/app/main.cpp",
        'const bool isStatic = ::lstrcmpiW(cls, L"STATIC") == 0;',
        'const bool isStatic = ::wcscmp(cls, L"STATIC") == 0;   // seeded',
        "class_case",
    ),

]


# v1.3.0-beta8 (BS-16): the PAINT rules are a source-level gate as well, and a
# gate that has never been red proves nothing — the whole point of this round is
# that four green CI rounds sat on top of a screen the users photographed as
# corrupted. Each seed removes exactly one mechanism that corruption needed, and
# the gate must fail naming it.
PAINT_SEEDS: list[tuple[str, str, str, str, str]] = [
    (
        "BS-16b  a child that does not clip against its siblings",
        "src/app/main.cpp",
        "style | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,",
        "style | WS_CHILD | WS_VISIBLE,",
        "WS_CLIPSIBLINGS",
    ),
    (
        "BS-17  a DPI rescale that keeps the stale baselines",
        "src/app/main.cpp",
        # Anchored on the InvalidateRect that precedes it in applySettingsDpiScale:
        # the probe's font-scale path drops the baseline too, so the bare call line
        # is not unique any more (v1.3.0-beta8fix1).
        "    dropSettingsLayoutBaseline();\n"
        "    ::InvalidateRect(g.hSettings, nullptr, TRUE);",
        "    /* seeded: stale baselines kept */\n"
        "    ::InvalidateRect(g.hSettings, nullptr, TRUE);",
        "drops the layout baseline",
    ),
    (
        "BS-16a  a tab switch that leaves the old tab on screen",
        "src/app/main.cpp",
        "    settingsRepaintAll(g.hSettings);\n",
        "    /* seeded: no repaint */\n",
        "showTab",
    ),
    (
        "BS-16d  the reflow request deduped by text again",
        "src/app/main.cpp",
        "!ok::layout::shouldRequestReflow(asked, need)",
        "(need > asked)",
        "dedupes by required height",
    ),
    (
        "BS-17  the solver reading the live (scrolled) rectangle again",
        "src/app/main.cpp",
        # BS-22 moved the un-scrolling to the capture point, so the seed now removes
        # the offset add-back itself (the scrolled live rectangle becomes the input)
        "            const ok::layout::Rect unscrolled =\n"
        "                ok::layout::solverInputRect(spec.rect, g_settingsScroll.offset);",
        "            const ok::layout::Rect unscrolled = spec.rect;   // seeded: the render, not the layout",
        "no longer adds the scroll offset back",
    ),
    (
        "RS-06  a report without the build identity",
        "src/app/main.cpp",
        "    out = buildIdentityUtf8() + out;",
        "    /* seeded: no identity */",
        "no longer part of the diagnostics report",
    ),
    (
        "BS-17  the probe's blank-page invariant deleted",
        "tools/ui_probe/ui_probe.cpp",
        "void checkEmptyPage(const Audit& a, const std::vector<Ctl>& ctls, int travelPx) {",
        "void checkEmptyPageRemoved(const Audit& a, const std::vector<Ctl>& ctls, int travelPx) {",
        "checkEmptyPage",
    ),
    (
        "BS-22c the clamp widens a narrow row again",
        "src/app/DialogLayout.hpp",
        "    if (out.w > room) { out.w = room; }              // narrow, never widen",
        "    if (out.w < minWidthPx) { out.w = minWidthPx; }\n"
        "    if (out.w > room) { out.w = room; }",
        "carries a minimum width again",
    ),
    (
        "BS-22c the region comes from the baseline again",
        "src/app/main.cpp",
        "        const ok::layout::ScrolledChild shown = ok::layout::scrollChildRect(",
        "        const ok::layout::ScrolledChild shown = sc;   // seeded: from the baseline",
        "computes a control's region from the solved baseline again",
    ),
    (
        "BS-22c the solve writes the bar pair by hand again",
        "src/app/main.cpp",
        "    const bool barChanged = settingsApplyScrollbarLatch(hwnd, anyScroll,\n"
        "                                                       \"solve-planned\");",
        "    const bool barChanged = anyScroll !=\n"
        "        ((::GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL) != 0);\n"
        "    ::SetWindowLongPtrW(hwnd, GWL_STYLE,\n"
        "                        anyScroll ? (::GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_VSCROLL)\n"
        "                                  : (::GetWindowLongPtrW(hwnd, GWL_STYLE) &\n"
        "                                     ~static_cast<LONG_PTR>(WS_VSCROLL)));\n"
        "    g_settingsScroll.enabled = anyScroll;",
        "writes the bar latch / WS_VSCROLL pair by hand",
    ),
    (
        "BS-22c the solver imposes the authored combo height again",
        "src/app/main.cpp",
        "        if (isCombo && liveH > 0) {\n"
        "            // The height the WINDOW has, not the authored one — and it goes in\n"
        "            // its own field rather than into `spec.rect.h` so that the rectangle\n"
        "            // handed to the solver stays the AUTHORED one (BS-22), while the\n"
        "            // engine still learns that this control already takes more space and\n"
        "            // moves the rows below it (see ControlSpec::liveHeight).\n"
        "            spec.liveHeight = liveH;\n"
        "        }",
        "        /* seeded: the authored height wins over Win32's */",
        "imposes its own height on a COMBO BOX again",
    ),
    (
        "BS-22c the probe's state mirror drifts from the app's",
        "tools/ui_probe/ui_probe.cpp",
        "    int barPos, barPage, barMax;\n    UINT dpi;",
        "    UINT dpi;\n    int barPos, barPage, barMax;",
        "does not mirror the app's",
    ),
    (
        "BS-22d the strip style stops tracking the plan",
        "src/app/main.cpp",
        "        if (tabPlan.multiline != hasMultiline) {\n"
        "            ::SetWindowLongPtrW(tabCtl, GWL_STYLE,\n"
        "                                tabPlan.multiline\n"
        "                                    ? (tabStyle | TCS_MULTILINE)\n"
        "                                    : (tabStyle & ~static_cast<LONG_PTR>(TCS_MULTILINE)));\n"
        "        }",
        "        if (false) {   // seeded: only the grow direction sets the style\n"
        "        }",
        "TCS_MULTILINE style is never CLEARED again",
    ),
    (
        "BS-22d the strip stops re-laying out when it shrinks",
        "src/app/main.cpp",
        "        const bool stripReshaped = (stripRows != g_settingsScroll.stripRows);",
        "        const bool stripReshaped = false;   // seeded: only the grow direction",
        "stops re-laying the tab strip out",
    ),
    (
        "BS-23 the row stops being measured in width",
        "src/app/main.cpp",
        "                    spec.requiredWidth = measureSingleLineWidthPx(c, label, len, pad);",
        "                    spec.requiredWidth = 0;   // seeded: height only",
        "no longer measures a single-line row's width",
    ),
    (
        "BS-23 the width fit forgets its sibling bound",
        "src/app/DialogLayout.hpp",
        "            limit = std::min(limit, orc.x - kRowGapPx);",
        "            limit = std::min(limit, 1000000);   // seeded: no sibling bound",
        "the sibling bound of the width fit",
    ),
    (
        "BS-22c the strip cycle stops proving it wrapped",
        "tools/ui_probe/ui_probe.cpp",
        "            if (measurable &&\n"
        "                (!wrappedRows || wrapGrow <= 0 || wrapped.page.top < basePageTop)) {",
        "            if (false) {   // seeded: no proof the labels wrapped",
        "the cycle refusing to report green when the labels did not wrap",
    ),
    (
        "BS-22c the latch stops following the window",
        "src/app/main.cpp",
        "        settingsApplyScrollbarLatch(hwnd, have, \"win32-visibility\");",
        "        /* seeded: the latch is not re-adopted */",
        "no longer re-adopted from the window",
    ),
    (
        "BS-22c a scroll-state write skips the re-adopt",
        "src/app/main.cpp",
        # The prose is part of the anchor: four call sites carry the adopt, and
        # this is the one whose comment names the unconditional rule.
        "            // state can be added without one (BS-22c).\n"
        "            settingsAdoptScrollbarVisibility(hwnd);",
        "            /* seeded: the write never re-reads the window bit */",
        "is not followed by settingsAdoptScrollbarVisibility",
    ),
    (
        "BS-22f the factory answers a missing face with another",
        "src/app/main.cpp",
        # The exact shape the 8ba7da0 run's font walk ran into: mint the face,
        # cache it if there is room, and otherwise hand back whatever is in slot 0.
        "        if (freeSlot != FontCache::kSlots) {\n"
        "            cache.slots[freeSlot] = FontCache::Entry{dpi, weight, px96, f};\n"
        "        }\n"
        "        return f;",
        "        if (freeSlot != FontCache::kSlots) {\n"
        "            cache.slots[freeSlot] = FontCache::Entry{dpi, weight, px96, f};\n"
        "            return f;\n"
        "        }\n"
        "        return cache.slots[0].font;",
        "answers a face it does not have with another face",
    ),
    (
        "BS-22f the font cache shrinks below what a session asks for",
        "src/app/main.cpp",
        "    static constexpr std::size_t kSlots = 48;",
        "    static constexpr std::size_t kSlots = 8;",
        "the font cache holds 8 faces",
    ),
    (
        "BS-22f the text scale forgets the app's own faces",
        "src/app/main.cpp",
        # BS-22w added the recorded role to every mapping pair; the anchor
        # follows the call's signature.
        "        ctx.add(app[role], next[role], role);          // what the app itself applies",
        "        /* seeded: only the faces this probe applied are mapped */",
        "no longer maps the app's own faces at the CURRENT dpi",
    ),
    (
        "BS-22h the strip cycle forgets the height it started from",
        "tools/ui_probe/ui_probe.cpp",
        "        const int baseShift = base.app.stripShift[tab];",
        "        /* seeded: the cycle does not record the height it started from */",
        "no longer compares the shift with the value it started from",
    ),
    (
        "BS-22o the strip cycle stops proving the labels wrap",
        "tools/ui_probe/ui_probe.cpp",
        "        stripW = std::max(wideW, stripW * 4 / 5);   // still one row: narrower",
        "        /* seeded: no search for a client where the labels wrap */",
        "stripW = std::max(wideW, stripW * 4 / 5);   // still one row: narrower is gone",
    ),
    (
        "BS-22g the blank-page finding stops cross-checking the render",
        "tools/ui_probe/ui_probe.cpp",
        "            const int renderedPts = renderPaintCountAt(\n"
        "                dlg, samples,\n"
        "                POINT{static_cast<int>(s.page.left) + 2, static_cast<int>(s.page.top) + 2});",
        "            const int renderedPts = 0;   // seeded: no cross-check",
        "const int renderedPts = renderPaintCountAt( is gone",
    ),
    (
        "BS-22i the native pass stops requiring a measurable transition",
        "tools/ui_probe/ui_probe.cpp",
        "        if (passDpi == nativeDpi) {",
        "        if (false) {   // seeded: nothing requires the native scale to measure",
        "MUST be measurable at the",
    ),
    (
        "BS-22k the chrome evidence goes back to the page's control list",
        "tools/ui_probe/ui_probe.cpp",
        "            for (HWND c = ::GetWindow(dlg, GW_CHILD); c != nullptr && chromeJudged < 4;\n"
        "                 c = ::GetWindow(c, GW_HWNDNEXT)) {",
        "            for (const HarnessCtl& c : s.ctls) {",
        "chromeJudged < 4; is gone",
    ),
    (
        "BS-22k an unusable capture is reported as a blank page again",
        "tools/ui_probe/ui_probe.cpp",
        "            if (judged >= 3 && painted == 0 && renderedPts > 0 &&\n"
        "                chromeJudged > 0 && chromePainted == 0) {",
        "            if (judged >= 3 && painted == 0) {",
        "renderedPts > 0 is gone",
    ),
    (
        "BS-22o the strip cycle accepts a widened client the app will shrink",
        "tools/ui_probe/ui_probe.cpp",
        "    if (!baseOneRowFinal) { measurable = false; }",
        "    /* seeded: the widened client counts as the cycle's own state */",
        "the rule that both halves of the transition have to hold on that state",
    ),
    (
        "BS-22v the tab labels are measured with the app's face, not the control's",
        "src/app/main.cpp",
        "        HGDIOBJ oldFont = ::SelectObject(tdc, tabLabelFont);",
        "        HGDIOBJ oldFont = ::SelectObject(tdc, uiFont());   /* seeded */",
        "the tab labels measured with the font the CONTROL wears",
    ),
    (
        "BS-22v the probe stops comparing the plan's face with the control's",
        "tools/ui_probe/ui_probe.cpp",
        "                        \"the plan measured the nine tab labels with a different font \"",
        "                        \"\" +   /* seeded: the two faces are not compared */",
        "the invariant that holds the two faces together",
    ),
    (
        "BS-22u the strip cycle stops reporting the plan's own arithmetic",
        "tools/ui_probe/ui_probe.cpp",
        "            \" plan \" + std::to_string(st.app.stripPlanRows) + \" need \" +",
        "            \"\" +   /* seeded: the plan's numbers are not reported */",
        "the strip decision",
    ),
    (
        "BS-22t the revealed page is left unrepainted after the z-order change",
        "src/app/main.cpp",
        "        ::RedrawWindow(hwnd, &pageRc, nullptr,\n"
        "                       RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);",
        "        /* seeded: the revealed page waits for the next WM_PAINT */",
        "the synchronous page repaint after the tab control is put at the bottom",
    ),
    (
        "BS-22s the page children stop being kept above the tab control",
        "src/app/main.cpp",
        "    ::SetWindowPos(tabCtl, HWND_BOTTOM, 0, 0, 0, 0,\n"
        "                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);",
        "    /* seeded: the z-order is left to chance */",
        "the z-order rule that keeps the page children ABOVE the tab control",
    ),
    (
        "BS-22r the render cross-check counts an unpainted frame as ink",
        "tools/ui_probe/ui_probe.cpp",
        "        if (!haveBg) { painted = -1; }",
        "        /* seeded: an undrawn buffer counts as the app's own render */",
        "the rule that a render which never painted its own background",
    ),
    (
        "BS-22q the screen-paint check stops naming the page area",
        "tools/ui_probe/ui_probe.cpp",
        "            const bool pagePaintedOnScreen = bareInCapture && (bareScreen != bg);",
        "            const bool pagePaintedOnScreen = true;   // seeded: never ask",
        "the measurement that tells",
    ),
    (
        "BS-22o the app stops measuring the tab control's own layout",
        "src/app/main.cpp",
        "                    if (dy >= rowH / 2 || -dy >= rowH / 2) { rows = 2; }",
        "                    rows = 1;   /* seeded: one row, whatever the layout says */",
        "the rule that reads the row count from those rectangles",
    ),
    (
        "BS-22n a combo that resizes itself asks for no reflow",
        "src/app/main.cpp",
        "    if (msg == WM_WINDOWPOSCHANGED && !g_settingsReflowPosted && g_settingsSolveDepth == 0) {",
        "    if (false) {   // seeded: the resize the app did not ask for is ignored",
        "is gone",
    ),
    (
        "BS-22m the strip cycle squeezes the pass into a 96-dpi-tall window",
        "tools/ui_probe/ui_probe.cpp",
        "    const int clientH = static_cast<int>(origClient.bottom);",
        "    const int clientH = ::MulDiv(static_cast<int>(origClient.bottom),\n"
        "                               static_cast<int>(passDpi), 96);   // seeded",
        "'s client HEIGHT is the pass's own pixel height",
    ),
    (
        "BS-22l a window that refused the planned size is not re-solved",
        "src/app/main.cpp",
        "            if (static_cast<int>(live.right - live.left) != plan.rects[i].w ||",
        "            if (false) {   // seeded: the window's own size is not read back",
        "is gone",
    ),
    (
        "BS-22l the live height stops moving the rows below it",
        "src/app/DialogLayout.hpp",
        "        if (c.liveHeight > 0) { want = c.liveHeight; }",
        "        // seeded: the rows below stay on the authored box",
        "the plan no longer takes the height the WINDOW has",
    ),
    (
        "BS-22l the combo class test goes back to a length guard",
        "src/app/main.cpp",
        "        const bool isCombo = ::lstrcmpiW(cls, L\"COMBOBOX\") == 0;",
        "        const bool isCombo = clsLen == 6 && ::lstrcmpiW(cls, L\"COMBOBOX\") == 0;",
        "is gone",
    ),
    (
        "BS-22k the pass audit stops checking the window against the plan",
        "tools/ui_probe/ui_probe.cpp",
        "            checkPlanHeld(a, ctls);",
        "            /* seeded: the pass audit does not compare the two */",
        "checkPlanHeld(a, ctls); is gone",
    ),
    (
        "BS-22k the harness stops checking the window against the plan",
        "tools/ui_probe/ui_probe.cpp",
        "        if (KieeKeyProbeSolvedRect(dlg, c.id, solved) == 0 || solved[2] <= 0) { continue; }",
        "        if (true) { continue; }   // seeded: the window is never compared",
        "KieeKeyProbeSolvedRect(dlg, c.id, solved) is gone",
    ),
    (
        "BS-22j the probe stops treating a page button as growable",
        "tools/ui_probe/ui_probe.cpp",
        "                         (isButton && (style & BS_MULTILINE) != 0);",
        "                         false;   // seeded: buttons are not measured",
        "(isButton && (style & BS_MULTILINE) != 0); is gone",
    ),
    (
        "BS-22j a page button stops being allowed to wrap",
        "src/app/main.cpp",
        "        style |= BS_MULTILINE;",
        "        /* seeded: page labels stay single-line */",
        "style |= BS_MULTILINE; is gone",
    ),
    (
        "BS-22j the solver stops growing page buttons",
        "src/app/main.cpp",
        "                        ((style & SS_TYPEMASK) != SS_OWNERDRAW)) || pageButton;",
        "                        ((style & SS_TYPEMASK) != SS_OWNERDRAW));",
        "|| pageButton; is gone",
    ),
    (
        "BS-22j the wrapped height is measured without the button's inset",
        "src/app/main.cpp",
        "            const int wrapW = (buttonTextPad > 0)\n"
        "                ? std::max(1, static_cast<int>(spec.rect.w) - buttonTextPad)\n"
        "                : static_cast<int>(spec.rect.w);",
        "            const int wrapW = static_cast<int>(spec.rect.w);   // seeded",
        "buttonTextPad) is gone",
    ),
    (
        "BS-22i the strip-cycle note stops being counted",
        "tools/ui_probe/ui_probe.cpp",
        "        ++g_stripCycleUnavailable;",
        "        /* seeded: an unmeasurable pass is not recorded */",
        "++g_stripCycleUnavailable; is gone",
    ),
    (
        "BS-22g the screen check counts controls its capture does not cover",
        "tools/ui_probe/ui_probe.cpp",
        "                if (mine.empty()) { ++uncovered; continue; }",
        "                /* seeded: an uncovered control counts as painted nothing */",
        "skipping a control its capture does not cover",
    ),
    (
        "BS-22g the rectangle findings forget the solver's own rect",
        "src/app/main.cpp",
        "extern \"C\" int KieeKeyProbeSolvedRect(HWND dlg, int id, int* out) {",
        "/* seeded: the solver's own rectangle is not answered */",
        "KieeKeyProbeSolvedRect() is gone",
    ),
    (
        "BS-22c a tab switch stops re-deciding the bar",
        "src/app/main.cpp",
        "    settingsSyncScrollbarLatch(g.hSettings);",
        "    /* seeded: the bar is never re-decided on a tab switch */",
        "no longer re-decides the bar",
    ),
    (
        "BS-22b the row is measured before it is clamped",
        "src/app/main.cpp",
        # The two blocks SWAPPED: the measurement runs first, the clamp second —
        # which is exactly the order the probe's I12 caught (a row grown for the
        # wide width, laid out at the narrow one, one line clipped).
        "        if (spec.tab != ok::layout::ControlSpec::kAlwaysVisible) {\n"
        "            spec.rect = ok::layout::clampPageChildWidth(spec.rect, clampLimitRight);\n"
        "        }\n"
        "        if (spec.growable) {\n"
        "            const int wrapW = (buttonTextPad > 0)\n"
        "                ? std::max(1, static_cast<int>(spec.rect.w) - buttonTextPad)\n"
        "                : static_cast<int>(spec.rect.w);\n"
        "            spec.requiredHeight = measureStaticTextHeightPx(c, wrapW);\n"
        "        }",
        "        if (spec.growable) {\n"
        "            const int wrapW = (buttonTextPad > 0)\n"
        "                ? std::max(1, static_cast<int>(spec.rect.w) - buttonTextPad)\n"
        "                : static_cast<int>(spec.rect.w);\n"
        "            spec.requiredHeight = measureStaticTextHeightPx(c, wrapW);\n"
        "        }\n"
        "        if (spec.tab != ok::layout::ControlSpec::kAlwaysVisible) {\n"
        "            spec.rect = ok::layout::clampPageChildWidth(spec.rect, clampLimitRight);\n"
        "        }   // seeded: measured first, clamped second",
        "measures a growable row's required height BEFORE",
    ),
    (
        "BS-22b the strip cycle demands a zero shift again",
        "tools/ui_probe/ui_probe.cpp",
        "            if (back.app.stripShift[tab] != baseShift && back.page.top == basePageTop) {",
        "            if (back.app.stripShift[tab] != 0) {",
        "no longer compares the shift with the value it started from",
    ),
    (
        "BS-22b the bar ruler goes back to all nine tabs",
        "tools/ui_probe/ui_probe.cpp",
        "        bool needBar = (a.contentBottom[tab] > s.page.bottom);",
        "        bool needBar = false;\n"
        "        for (int t = 0; t < 9; ++t) { if (a.contentBottom[t] > s.page.bottom) { needBar = true; } }",
        "the bar ruler is no longer the CURRENT",
    ),
    (
        "BS-22  the solver reads the live rectangle again",
        "src/app/main.cpp",
        "            if (const ok::layout::Rect* authored = authoredPageRect(id)) {",
        "            if (const ok::layout::Rect* authored = (const ok::layout::Rect*)nullptr) {",
        "no longer reads the AUTHORED rectangle",
    ),
    (
        "BS-21  the tab-strip shift accumulates again",
        "src/app/main.cpp",
        "        const ok::layout::StripShift shift = ok::layout::stripShiftFor(",
        "        const ok::layout::StripShift shift = ok::layout::pageTopShiftPx(",
        "no longer goes through",
    ),
    (
        "BS-20  the solver stops clamping rows to the page",
        "src/app/main.cpp",
        # BS-22b moved the clamp into the build loop (it must run before the text is
        # measured), so the seed mutates it where it now lives.
        "        if (spec.tab != ok::layout::ControlSpec::kAlwaysVisible) {\n"
        "            spec.rect = ok::layout::clampPageChildWidth(spec.rect, clampLimitRight);\n"
        "        }",
        "        /* seeded: the page's width is not a bound any more */",
        "no longer clamps page children",
    ),
    (
        "BS-19  the scale-pass handover stops being asserted",
        "tools/ui_probe/ui_probe.cpp",
        # anchored on the DPI half of the contract: the counter alone appears
        # twice (the handover asserts both the DPI and the client size). Slot 16
        # since v1.3.0-beta8fix2 moved R1 out of slot 13 to make room for
        # I13/I14/I15.
        "++g_invChecks[16];\n        if (handed.app.dpi != passDpi) {",
        "if (handed.app.dpi != passDpi) {",
        "the handover assertion itself",
    ),
    (
        "BS-19  the report stops validating itself before writing",
        "tools/ui_probe/ui_probe.cpp",
        "if (!jsonBalanced(json)) {",
        "if (false) {",
        "the probe must refuse to publish an unparseable report",
    ),
    (
        "BS-18  the baseline drop forgets the scroll position",
        "src/app/main.cpp",
        # anchored on the call plus the BS-21 normalisation that now follows it
        # (the bare call line is not adjacent to solved.clear() any more)
        "    settingsScrollToTop();\n"
        "    // v1.3.0-beta8fix1 (bug BS-21): and it may not be discarded",
        "    // v1.3.0-beta8fix1 (bug BS-21): and it may not be discarded",
        "may not discard the baseline while the page is scrolled",
    ),
    (
        "BS-18  the DPI rescale scales the scrolled rectangles",
        "src/app/main.cpp",
        "    settingsScrollToTop();\n\n    // Snapshot the OLD fonts",
        "\n\n    // Snapshot the OLD fonts",
        "must put the page back on its baseline first",
    ),
    (
        "BS-18  the DPI rescale stops re-solving",
        "src/app/main.cpp",
        "    ::InvalidateRect(g.hSettings, nullptr, TRUE);\n"
        "    solveSettingsLayout(g.hSettings);",
        "    ::InvalidateRect(g.hSettings, nullptr, TRUE);\n"
        "    /* seeded: scaled but not solved */",
        "scales the children without",
    ),
    (
        "BS-18  the baseline drop keeps the scroll range",
        "src/app/main.cpp",
        # BS-22c routed the bar pair through its one owner, so the anchor is the
        # range/depths pair the drop still has to zero.
        "    g_settingsScroll.range = 0;\n"
        "    // One owner: the latch and the bit move together, or neither moves.",
        "    // One owner: the latch and the bit move together, or neither moves.",
        "no longer clears the scroll range",
    ),
    (
        "BS-18  the settings window stops handling WM_DISPLAYCHANGE",
        "src/app/main.cpp",
        "        case WM_DISPLAYCHANGE:\n"
        "            // v1.3.0-beta8fix1 (bug BS-18): the settings dialog is a TOP-LEVEL",
        "        case WM_DISPLAYCHANGE_IGNORED:\n"
        "            // v1.3.0-beta8fix1 (bug BS-18): the settings dialog is a TOP-LEVEL",
        "no longer handles WM_DISPLAYCHANGE",
    ),
    (
        "BS-18  the probe's invariant battery deleted",
        "tools/ui_probe/ui_probe.cpp",
        # v1.3.0-beta8fix2: the battery also takes the dialog's COMPLETE child
        # set now (the hide-set can only be judged across the unselected tabs).
        "void harnessAssert(HWND dlg, const HarnessState& s, const std::vector<HWND>& all,",
        "void harnessAssertDisabled(HWND dlg, const HarnessState& s, const std::vector<HWND>& all,",
        "harnessAssert",
    ),
    (
        "BS-16e  the probe's z-order rule deleted",
        "tools/ui_probe/ui_probe.cpp",
        "void checkSiblingClobber(const Audit& a, const std::vector<Ctl>& ctls) {",
        "void checkSiblingClobberRemoved(const Audit& a, const std::vector<Ctl>& ctls) {",
        "checkSiblingClobber",
    ),
    # v1.3.0-beta8fix1 (BS-22w): the scale must know every face the app minted,
    # the restore must be audited before the capture runs, and the ink samples
    # must land strictly inside each control. Same discipline, five seeds.
    (
        "BS-22w  the rescale keeps its faces unreported",
        "src/app/main.cpp",
        "        kieeKeyProbeRememberAppFont(replacement, replacementRole);",
        "        /* seeded: the rescale's faces go unreported */",
        "BS-22w",
    ),
    (
        "BS-22w  the scale guesses an app-minted face's role",
        "src/app/main.cpp",
        "        const int role = g_probeAppFontRoles[i];",
        "        const int role = 0;   /* seeded: role guessed, not recorded */",
        "BS-22w",
    ),
    (
        "BS-22w  the restore audit stops counting",
        "src/app/main.cpp",
        "            ++g_probeUnmappedFonts;",
        "            /* seeded: the audit never counts */",
        "BS-22w",
    ),
    (
        "BS-22w  the capture runs without reading the audit",
        "tools/ui_probe/ui_probe.cpp",
        "            const int unmapped = KieeKeyProbeUnmappedFontCount();",
        "            const int unmapped = 0;   /* seeded: the audit is never read */",
        "BS-22w",
    ),
    (
        "BS-22w  the ink samples leave the control's interior",
        "tools/ui_probe/ui_probe.cpp",
        "                            const int x = ex + ew * col / 5;",
        "                            const int x = ex + ew;   /* seeded: off the control */",
        "BS-22w",
    ),
    # v1.3.0-beta8fix1 (BS-22w follow-up): the reflow invariant is judged on
    # the CONVERGED reflow, and the evidence that separates a stale-input
    # correction from drift is mandatory. Same discipline, two seeds.
    (
        "BS-22w  the reflow finding loses the strip's own shape",
        "tools/ui_probe/ui_probe.cpp",
        "std::string reflowStripShape(HWND dlg) {",
        "std::string reflowStripShapeRemoved(HWND dlg) {   /* seeded: helper gone */",
        "cannot be told from a drift",
    ),
    (
        "BS-22w  the reflow convergence clause deleted",
        "tools/ui_probe/ui_probe.cpp",
        "                            \"the reflow did not converge: a second identical \"",
        "                            \"/* seeded: convergence never judged */ \"",
        "the reflow did not converge",
    ),
    # v1.3.0-beta8fix2 (BS-23a/BS-23b/BS-23c): THE PHOTOGRAPH HAS A HARNESS.
    # The user's 150 % photograph (double title, stale page, right-edge clip
    # under a visible bar, two-row strip at the default size) defines states no
    # earlier round could reach. These seeds prove the measurement contract is
    # load-bearing: the reopen entry point, the open-path scenario (open state,
    # tab sweep, width sweep), and the three invariants (I13 hide-set, I14
    # chrome band incl. the duplicate-title walk, I15 right edge).
    (
        "BS-23a  the reopen entry point deleted",
        "src/app/main.cpp",
        "extern \"C\" HWND KieeKeyProbeReopenSettings(HWND dlg, int tab) {",
        "extern \"C\" HWND KieeKeyProbeReopenSettingsRemoved(HWND dlg, int tab) {",
        "KieeKeyProbeReopenSettings",
    ),
    (
        "BS-23a  the reopen stops closing through the app's own path",
        "src/app/main.cpp",
        "    ::SendMessageW(dlg, WM_CLOSE, 0, 0);",
        "    /* seeded: the dialog is never closed, only re-opened over it */",
        "the reopen closes through the app's own WM_CLOSE path",
    ),
    (
        "BS-23a  the open-path scenario deleted",
        "tools/ui_probe/ui_probe.cpp",
        "int harnessScenarioOpenGeometry(HWND* dlgInOut, int tabCount, unsigned passDpi,",
        "int harnessScenarioOpenGeometryRemoved(HWND* dlgInOut, int tabCount, unsigned passDpi,",
        "the open-path scenario",
    ),
    (
        "BS-23a  the width sweep shrinks to a single width",
        "tools/ui_probe/ui_probe.cpp",
        # anchored with the line above: the full-height sweep (E5) has the
        # same loop header, and only the round-1 sweep may shrink here.
        "            const int sweepH = static_cast<int>(openedClient.bottom);\n"
        "            for (int w = 560; w <= 1000; w += 20) {",
        "            const int sweepH = static_cast<int>(openedClient.bottom);\n"
        "            for (int w = 560; w <= 560; w += 20) {   /* seeded: one width, no sweep */",
        "the width sweep",
    ),
    (
        "BS-23b  the hide-set invariant stops judging",
        "tools/ui_probe/ui_probe.cpp",
        "        if (shown != (page == tab)) {",
        "        if (false) {   /* seeded: the hide-set is never judged */",
        "shown != (page == tab)",
    ),
    (
        "BS-23b  the duplicate-title walk allows every copy",
        "tools/ui_probe/ui_probe.cpp",
        "                const bool allowedInfoName = id == IDC_STAT_INFO_NAME && tab == 4;",
        "                const bool allowedInfoName = true;   /* seeded: every copy allowed */",
        "IDC_STAT_INFO_NAME && tab == 4",
    ),
    (
        "BS-23c  the right-edge invariant stops judging",
        "tools/ui_probe/ui_probe.cpp",
        "            if (effRight > s.page.right + 1) {",
        "            if (false) {   /* seeded: the right edge is never judged */",
        "effRight > s.page.right + 1",
    ),
    # v1.3.0-beta8fix2 round 2 (docs/HYPOTHESES_BS23_v1.3.0-beta8fix2.md):
    # round 1 (run 36122792718) measured the open path clean, so the
    # discriminants that separate the user's machine from the runner must stay
    # load-bearing — E1 the tray-open sequence, E2 the landed tick, E3 the
    # growth row, E4 the real WM_DPICHANGED.
    (
        "BS-23b  the tray-open sequence shrinks to tab 0",
        "tools/ui_probe/ui_probe.cpp",
        "    static const int kOpenTabs[] = {0, 4, 8};   // E1: the tray-open sequence",
        "    static const int kOpenTabs[] = {0};   /* seeded: only tab 0 is opened */",
        "E1 the tray-open sequence",
    ),
    (
        "BS-23a  the landed tick stops being judged",
        "tools/ui_probe/ui_probe.cpp",
        "                harnessAssert(dlg, st, all, \"scenario_open_tick\", 0, 0, 100, findings);",
        "                /* seeded: the landed tick is never judged */",
        "E2 a tick that LANDS",
    ),
    (
        "BS-23a  the growth row stops being judged",
        "tools/ui_probe/ui_probe.cpp",
        "                harnessAssert(dlg, st, all, \"scenario_open_growth\", 0, 0, 100, findings);",
        "                /* seeded: the growth row is never judged */",
        "E3 a growth row",
    ),
    (
        "BS-23c  the real WM_DPICHANGED is never sent",
        "tools/ui_probe/ui_probe.cpp",
        "            ::SendMessageW(dlg, WM_DPICHANGED, static_cast<WPARAM>(upDpi),",
        "            /* seeded: the monitor-move message is never sent */",
        "E4 a REAL WM_DPICHANGED",
    ),
    (
        "BS-23c  the work-area override stops reaching the refit",
        "src/app/main.cpp",
        "        rcWork = g_probeWorkAreaOverride;",
        "        /* seeded: the override never reaches the refit */",
        "the override is the LAST word on the bound refitWindow receives",
    ),
    (
        "BS-23c  the full-height open deleted",
        "tools/ui_probe/ui_probe.cpp",
        "        KieeKeyProbeSetWorkAreaOverride(0, 0, 1280, 1600);",
        "        KieeKeyProbeSetWorkAreaOverride(0, 0, 0, 0);   /* seeded: no headroom */",
        "E5 the full-height open",
    ),
    # v1.3.0-beta8fix2 round 4: the user's facts (the breakage persists across
    # reopens; a tab click produces it, especially tab 8) point at a
    # paint-level state — the stale-pixel audit must run at the photographed
    # geometry, not only at the pass geometry.
    (
        "BS-23b  the open-geometry pixel audit deleted",
        "tools/ui_probe/ui_probe.cpp",
        "                pa.prefix = \"open@\" + std::to_string(passDpi) +",
        "                pa.prefix = std::string(\"(seeded) \") +",
        "E6 the pixel truth",
    ),
]

# Files scripts/check_dialog_paint_rules.py reads, relative to the repo root.
PAINT_INPUTS = (
    "src/app/main.cpp",
    "src/app/DialogLayout.hpp",
    "tools/ui_probe/ui_probe.cpp",
)


def run_paint_rules(root: pathlib.Path) -> tuple[int, str]:
    proc = subprocess.run(
        [sys.executable, str(root / "scripts" / "check_dialog_paint_rules.py"),
         f"--repo={root}"],
        capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def run_audit(root: pathlib.Path) -> tuple[int, str]:
    proc = subprocess.run(
        [sys.executable, str(root / "scripts" / "audit_layout.py"),
         "--strict", f"--repo={root}"],
        capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def main() -> int:
    failures = 0

    # 0. The tree as shipped must be green — otherwise every seed below is
    #    meaningless (the audit would be red for an unrelated reason).
    rc, out = run_audit(REPO)
    if rc != 0:
        print("FAIL: the un-seeded tree is not green — fix that first")
        print(out)
        return 1
    print("  [ok] un-seeded tree: AUDIT OK")

    for name, rel, old, new, kind in SEEDS:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "scripts").mkdir(parents=True)
            (root / "src" / "app").mkdir(parents=True)
            shutil.copy2(REPO / "scripts" / "audit_layout.py", root / "scripts" / "audit_layout.py")
            for f in ("main.cpp", "ChaosLabWindow.cpp"):
                shutil.copy2(REPO / "src" / "app" / f, root / "src" / "app" / f)
            target = root / rel
            text = target.read_text(encoding="utf-8")
            if text.count(old) != 1:
                print(f"  [skipped] {name}: seed anchor not found ({text.count(old)} matches)"
                      f" — the audit changed, update tests/verify_audit_seeds.py")
                failures += 1
                continue
            target.write_text(text.replace(old, new, 1), encoding="utf-8")
            rc, out = run_audit(root)
            caught = rc != 0 and kind in out
            print(f"  [{'ok' if caught else 'MISS'}] {name} -> expected a {kind!r} finding")
            if not caught:
                failures += 1
                print(out)

    # v1.3.0-beta8 (BS-16): the paint rules, same discipline.
    rc, out = run_paint_rules(REPO)
    if rc != 0:
        print("FAIL: the un-seeded tree does not pass the paint rules — fix that first")
        print(out)
        return 1
    print("  [ok] un-seeded tree: PAINT RULES OK")

    for name, rel, old, new, kind in PAINT_SEEDS:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "scripts").mkdir(parents=True)
            shutil.copy2(REPO / "scripts" / "check_dialog_paint_rules.py",
                         root / "scripts" / "check_dialog_paint_rules.py")
            for relpath in PAINT_INPUTS:
                dst = root / relpath
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(REPO / relpath, dst)
            target = root / rel
            text = target.read_text(encoding="utf-8")
            if text.count(old) != 1:
                print(f"  [skipped] {name}: seed anchor not found ({text.count(old)} matches)"
                      f" — the source changed, update tests/verify_audit_seeds.py")
                failures += 1
                continue
            target.write_text(text.replace(old, new, 1), encoding="utf-8")
            rc, out = run_paint_rules(root)
            caught = rc != 0 and kind in out
            print(f"  [{'ok' if caught else 'MISS'}] {name} -> expected {kind!r}")
            if not caught:
                failures += 1
                print(out)

    if failures:
        print(f"\nSEED VERIFICATION FAILED — {failures} case(s)")
        return 1
    print("\nALL SEEDED VIOLATIONS CAUGHT (the audit is not vacuously green)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
