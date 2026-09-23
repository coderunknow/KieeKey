#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 coderunknow
"""Paint gate for the hand-rolled Win32 settings dialog (v1.3.0-beta8, BS-16).

CI was green for four rounds while the users' screens were visibly corrupted,
because every check — the static audits, the portable suites and the CA-03 UI
probe — measured WINDOW STATE. The corruption lived one layer below it: the
dialog owns every client pixel its ~120 children do not cover, and a sibling
without WS_CLIPSIBLINGS paints through the controls above it. Neither is visible
to a rectangle check, and the probe's screenshots were WM_PRINTCLIENT renders
(the app drawing on request), which can only ever show the correct frame.

These five rules are the source-level half of that fix. They are deliberately
narrow and exact: each one names the mechanism it protects, and each one fails
loudly if the mechanism is removed.

  1. mkCtl() adds WS_CLIPSIBLINGS to every control it creates.
  2. The settings window carries WS_CLIPCHILDREN and WS_EX_COMPOSITED.
  3. After a layout change the dialog repaints what it owns
     (settingsRepaintAll) — from showTab, from the scroll applier and from the
     solver.
  4. A live row asks for a reflow by REQUIRED HEIGHT, never by text hash
     (a counter that changes twice a second must not re-solve the dialog).

Run: python3 scripts/check_dialog_paint_rules.py --repo=.
"""
import argparse
import re
import sys
from pathlib import Path


def function_body(source, signature):
    """Body of `signature` with comments and literals stripped."""
    stripped = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                      '', source, flags=re.S)
    match = re.search(signature + r'\s*\{', stripped)
    if not match:
        raise ValueError(f"missing function: {signature}")
    start = match.end()
    depth = 1
    for i in range(start, len(stripped)):
        depth += (stripped[i] == '{') - (stripped[i] == '}')
        if depth == 0:
            return stripped[start:i]
    raise ValueError(f"unterminated function: {signature}")


def check(repo: Path):
    failures = []
    main = (repo / 'src/app/main.cpp').read_text(encoding='utf-8')

    # 1. every child the settings dialog creates clips against its siblings
    try:
        mkctl = function_body(main, r'HWND mkCtl\s*\(HWND parent, LPCWSTR cls, '
                                    r'LPCWSTR text, DWORD style, int x, int y,\s*'
                                    r'int w, int h, HMENU id\)')
    except ValueError as exc:
        return [f"main.cpp: {exc}"]
    if 'WS_CLIPSIBLINGS' not in mkctl:
        failures.append(
            "main.cpp: mkCtl() no longer adds WS_CLIPSIBLINGS — the tab control's "
            "rectangle is the whole page area and it is the lower-z sibling of every "
            "page control, so its repaint paints through them (BS-16b)")

    # 2. the window itself: no painting under children, one composited frame
    created = re.search(r'CreateWindowExW\s*\(([^,]+),\s*L"KieeKeySettings"', main)
    if not created:
        failures.append("main.cpp: the KieeKeySettings CreateWindowExW call is gone")
    # (BS-16c was reverted on purpose: WS_EX_COMPOSITED changed the painting
    # model of the whole subtree, and the tick's jitter is fixed at its source —
    # see rule 4 / BS-16d. The gate therefore does NOT require the style.)
    style_at = main.find('L"KieeKeySettings"')
    style_block = main[style_at:style_at + 1400] if style_at >= 0 else ''
    if 'WS_CLIPCHILDREN' not in style_block:
        failures.append(
            "main.cpp: the settings window no longer carries WS_CLIPCHILDREN "
            "(BS-13) — the class brush erases over the child controls on every "
            "repaint")

    # 3. after a state change, the dialog repaints the pixels it owns
    if 'void settingsRepaintAll(HWND hwnd)' not in main:
        failures.append("main.cpp: settingsRepaintAll() is gone — nothing repaints "
                        "the client area the children do not cover (BS-16a)")
    else:
        for name, pattern in (
                ('showTab', r'void showTab\s*\(int tab\)'),
                ('applySettingsScrollOffset', r'void applySettingsScrollOffset\s*\(HWND hwnd\)'),
                ('solveSettingsLayout', r'void solveSettingsLayout\s*\(HWND hwnd\)')):
            try:
                body = function_body(main, pattern)
            except ValueError:
                failures.append(f"main.cpp: {name}() not found")
                continue
            if 'settingsRepaintAll(' not in body:
                failures.append(
                    f"main.cpp: {name}() moves/hides controls without calling "
                    f"settingsRepaintAll() — the previous frame stays on screen "
                    f"(BS-16a)")

    # 4. the telemetry reflow is deduped by requirement, not by text
    try:
        refresh = function_body(main, r'void refreshGrowingRow\s*\(HWND dlg, int id, '
                                      r'const std::wstring& text\)')
    except ValueError:
        failures.append("main.cpp: refreshGrowingRow() not found")
    else:
        if 'shouldRequestReflow' not in refresh:
            failures.append(
                "main.cpp: refreshGrowingRow() no longer dedupes by required height — "
                "a live counter that changes its text twice a second re-solves the "
                "whole dialog and refits the window at 2 Hz (BS-16d)")
        if 'std::hash' in refresh:
            failures.append(
                "main.cpp: refreshGrowingRow() hashes the text again — that is the "
                "2 Hz reflow loop, see BS-16d")

    layout = (repo / 'src/app/DialogLayout.hpp').read_text(encoding='utf-8')
    if 'shouldRequestReflow' not in layout:
        failures.append("src/app/DialogLayout.hpp: the reflow policy "
                        "shouldRequestReflow() is gone (BS-16d)")

    # 6. the solver starts from the UNSCOLLED layout (BS-17). Reading the live
    #    page rectangle is the bug: 'scrolling' only renders, so a re-solve that
    #    takes the render as its input folds the scroll offset into the layout.
    try:
        solve = function_body(main, r'void solveSettingsLayout\s*\(HWND hwnd\)')
    except ValueError:
        failures.append("main.cpp: solveSettingsLayout() not found")
    else:
        if ('solverInputRect' not in solve or
                'g_settingsScroll.solved' not in solve or
                'spec.rect.y = y;' not in solve):
            failures.append(
                "main.cpp: solveSettingsLayout() no longer derives the page children's "
                "input from the saved baseline / solverInputRect (the y it applies to "
                "the spec) — a reflow while the user is scrolled then folds the scroll "
                "offset into the layout and the page drifts away (BS-17)")
    if 'void dropSettingsLayoutBaseline() noexcept' not in main:
        failures.append("main.cpp: dropSettingsLayoutBaseline() is gone — a DPI "
                        "rescale would feed the old scale's baselines back in (BS-17)")
    elif 'dropSettingsLayoutBaseline();' not in main.split('void applySettingsDpiScale')[1][:1200]:
        failures.append("main.cpp: the DPI rescale no longer drops the layout "
                        "baseline (BS-17)")
    layout_src = (repo / 'src/app/DialogLayout.hpp').read_text(encoding='utf-8')
    if 'solverInputRect' not in layout_src:
        failures.append("src/app/DialogLayout.hpp: solverInputRect() is gone (BS-17)")

    # 7. the build identity (RS-06): the app must be able to say which build it
    #    is, from the artefact itself.
    if 'buildIdentityUtf8' not in main or 'exeSha256Hex' not in main:
        failures.append("main.cpp: the build identity block is gone — a report can "
                        "no longer be tied to a revision (RS-06)")
    elif 'buildIdentityUtf8() + out' not in main:
        failures.append("main.cpp: the build identity is no longer part of the "
                        "diagnostics report (RS-06)")

    probe = (repo / 'tools/ui_probe/ui_probe.cpp').read_text(encoding='utf-8')
    for needle, why in (
            ('void checkEmptyPage(', 'the blank-page invariant (BS-17)'),
            ('void checkReflowWhileScrolled(', 'the scroll-then-reflow check (BS-17)'),
            ('void checkSiblingClobber(',
             'the z-order rule that let a sibling paint through the page (BS-16b)'),
            ('void checkStalePixels(',
             'the real-screen staleness check: CI otherwise only ever sees '
             'WM_PRINTCLIENT renders (BS-16e)'),
            ('readScreenClient', 'the screen read the staleness check needs')):
        if needle not in probe:
            failures.append(f"tools/ui_probe/ui_probe.cpp: {needle} is gone — {why}")
    return failures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.')
    args = ap.parse_args()
    failures = check(Path(args.repo))
    if failures:
        print("PAINT RULES: FAIL")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("PAINT RULES OK — 5 rules: sibling clipping, window styles, "
          "repaint-after-change, need-deduped reflow, unscrolled solver input "
          "(+ the probe's blank-page, reflow and pixel checks)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
