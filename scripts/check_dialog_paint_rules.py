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
    else:
        extended = created.group(1)
        if 'WS_EX_COMPOSITED' not in extended:
            failures.append(
                "main.cpp: the settings window is created without WS_EX_COMPOSITED — "
                "the 500 ms telemetry tick then repaints controls straight onto the "
                "desktop (the 'giật giật' report, BS-16c)")
    style_at = main.find('L"KieeKey — Cài đặt & Thông tin"')
    style_block = main[style_at:style_at + 900] if style_at >= 0 else ''
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

    probe = (repo / 'tools/ui_probe/ui_probe.cpp').read_text(encoding='utf-8')
    for needle, why in (
            ('checkSiblingClobber',
             'the z-order rule that let a sibling paint through the page (BS-16b)'),
            ('checkStalePixels',
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
    print("PAINT RULES OK — 4 rules: sibling clipping, window styles, "
          "repaint-after-change, need-deduped reflow (+ the probe's pixel checks)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
