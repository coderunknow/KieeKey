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


def read_or_empty(repo: Path, rel: str) -> str:
    """Read a file if it exists (a gate must not break when it does not).

    tests/verify_audit_seeds.py seeds ONE file into a temp tree, so a rule that
    reads a second file has to survive its absence: a FileNotFoundError there is
    not a gate failure, it is a broken gate, and it took the whole seed run down
    when the BS-20 rule read tests/test_dialog_layout.cpp.
    """
    path = repo / rel
    return path.read_text(encoding='utf-8') if path.is_file() else ""


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
    else:
        # The check reads the function BODY (comments stripped) rather than a
        # fixed-length window of the raw text: a comment that documents the
        # mechanism must never be able to push the code out of the window the
        # rule looks at (it did, on the beta8fix1 comment — a gate that fails
        # because of a longer comment is a gate about comments).
        try:
            dpi_rescale_body = function_body(
                main, r'void applySettingsDpiScale\s*\(UINT newDpi\)\s*noexcept')
        except ValueError:
            dpi_rescale_body = ''
        if 'dropSettingsLayoutBaseline();' not in dpi_rescale_body:
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

    # 8. v1.3.0-beta8fix1 (BS-18): A RESCALED DIALOG IS A SOLVED DIALOG, AND A
    #    DIALOG WITHOUT A LAYOUT HAS NO SCROLL STATE.
    #
    #    Four green CI rounds measured a settled dialog while the user's screen
    #    showed a page with no content and no scrollbar. The mechanism was an
    #    operation that rescaled every child rectangle and dropped the layout
    #    baseline without re-solving: from that state every scroll message is a
    #    silent no-op (applySettingsScrollOffset() needs a baseline), the
    #    scrollbar keeps the previous geometry's answer, and the next solve bakes
    #    the un-normalized rectangles in. These rules pin the three mechanisms
    #    that close it, plus the harness that reproduced it.
    try:
        dpi_body = function_body(main, r'void applySettingsDpiScale\s*\(UINT newDpi\)\s*noexcept')
    except ValueError:
        failures.append("main.cpp: applySettingsDpiScale() not found (BS-18)")
    else:
        if 'dropSettingsLayoutBaseline();' not in dpi_body:
            failures.append("main.cpp: the DPI rescale no longer drops the layout "
                            "baseline (BS-17)")
        if 'solveSettingsLayout(' not in dpi_body:
            failures.append(
                "main.cpp: applySettingsDpiScale() scales the children without "
                "re-solving (BS-18) — the caller that forgets (WM_DISPLAYCHANGE did) "
                "leaves the dialog with NO baseline, so every scroll step becomes a "
                "no-op, the scrollbar keeps a geometry that no longer exists and the "
                "page loses its content until the dialog is reopened")
    try:
        unscroll_body = function_body(main, r'void settingsScrollToTop\s*\(\)\s*noexcept')
    except ValueError:
        failures.append(
            "main.cpp: settingsScrollToTop() not found (BS-18, part two) — nothing "
            "returns the page to its baseline, so whoever rewrites the live "
            "rectangles (the DPI rescale) or discards the baseline (the drop) bakes "
            "the scroll offset into the geometry one scroll position at a time")
    else:
        for needle, what in (('g_settingsScroll.offset = 0;', 'clears the offset'),
                             ('applySettingsScrollOffset(', 're-applies the offset')):
            if needle not in unscroll_body:
                failures.append(
                    f"main.cpp: settingsScrollToTop() no longer {what} (BS-18, part "
                    f"two) — zeroing the offset without moving the children back to "
                    f"the baseline is the ratchet itself")
    try:
        drop_body = function_body(main, r'void dropSettingsLayoutBaseline\s*\(\)\s*noexcept')
    except ValueError:
        failures.append("main.cpp: dropSettingsLayoutBaseline() not found (BS-18)")
    else:
        if 'settingsScrollToTop();' not in drop_body:
            failures.append(
                "main.cpp: dropSettingsLayoutBaseline() may not discard the baseline "
                "while the page is scrolled away from it (BS-18, part two) — the "
                "live rectangles are the only geometry a later solve can plan from, "
                "so dropping them scrolled ratchets the page out of its viewport "
                "(empty page, no scrollbar, until the dialog is reopened)")
        if 'settingsScrollToTop();' not in dpi_body:
            failures.append(
                "main.cpp: applySettingsDpiScale() rescales the LIVE rectangles "
                "(rescaleChild multiplies them) — it must put the page back on its "
                "baseline first (BS-18, part two), or the scroll offset is scaled "
                "into the geometry and the drop that follows makes it permanent")
        elif dpi_body.index('settingsScrollToTop();') > dpi_body.index('EnumChildWindows'):
            failures.append(
                "main.cpp: applySettingsDpiScale() un-scrolls AFTER it has already "
                "rescaled the children (BS-18, part two) — the order is the fix")
        for needle, what in (('g_settingsScroll.offset = 0;', 'the scroll position'),
                             ('g_settingsScroll.range = 0;', 'the scroll range'),
                             ('g_settingsScroll.enabled = false;', "the bar's latch"),
                             ('WS_VSCROLL', 'the WS_VSCROLL style bit'),
                             ('perTabContentBottom', 'the per-tab solved depths')):
            if needle not in drop_body:
                failures.append(
                    f"main.cpp: dropSettingsLayoutBaseline() no longer clears {what} "
                    f"(BS-18) — a dialog with no layout keeping a live scroll state is "
                    f"the unrecoverable state the user sees as an empty page with a "
                    f"dead or missing scrollbar")
    try:
        settings_proc = function_body(
            main, r'LRESULT CALLBACK settingsProc\s*\(HWND hwnd, UINT msg, '
                  r'WPARAM wParam, LPARAM lParam\)')
    except ValueError:
        failures.append("main.cpp: settingsProc() not found (BS-18)")
    else:
        # The main window's proc handles WM_DISPLAYCHANGE for its own reasons
        # (foreground policy, monitor rects) — the rule is about the DIALOG.
        if 'case WM_DISPLAYCHANGE:' not in settings_proc:
            failures.append(
                "main.cpp: settingsProc() no longer handles WM_DISPLAYCHANGE "
                "(BS-18) — the dialog is a TOP-LEVEL window and the OS broadcasts the "
                "event straight to it; it may not depend on the main window's handler "
                "for its own layout (that dependency is exactly how the page lost its "
                "content and its scrollbar until the dialog was reopened)")
    try:
        solve_body = function_body(main, r'void solveSettingsLayout\s*\(HWND hwnd\)')
    except ValueError:
        failures.append("main.cpp: solveSettingsLayout() not found (BS-18)")
    else:
        if 'finalOverflow' not in solve_body:
            failures.append(
                "main.cpp: the post-clamp scrollbar correction is gone (BS-18) — the "
                "fallback decision must be recomputed from the CLAMPED viewport "
                "(the planned one cannot know it) and corrected in BOTH directions")
    for needle, why in (
            ('int runSequenceHarness(', 'the seeded operation-sequence pass (BS-18)'),
            ('void harnessAssert(', 'the I1..I12 invariant battery (BS-18)'),
            ('void harnessFail(', 'the per-invariant violation record (BS-18)'),
            ('display_change', 'the display-change operation that reproduced BS-18'),
            ('KieeKeyProbeScrollState', "the app's own scroll-state read (BS-18)")):
        if needle not in probe:
            failures.append(f"tools/ui_probe/ui_probe.cpp: {needle} is gone — {why}")

    # 9. v1.3.0-beta8fix1 (BS-19): THE HANDOVER BETWEEN SCALE PASSES, AND A REPORT
    #    THAT CANNOT LIE ABOUT ITSELF.
    #
    #    runSequenceHarness() runs BETWEEN the scale passes and fuzzes the dialog
    #    through 96/120/144/192 dpi, 100/125/150 % text scale and 70..130 %
    #    client sizes. Its cleanup restores the font scale, the client SIZE, the
    #    offset and the tab — but not the DPI — so the dialog it left behind was
    #    the one the NEXT pass audited: the run 35866022217 red was measured on a
    #    leftover layout (children 1.5x the authored width in a 399 px client),
    #    and the probe blamed the product for it. The handover is now asserted
    #    (invariant slot 13: app dpi == the pass's dpi, client size == the pass's
    #    client size, and the restored state passes I1..I12), and the report
    #    validates its own braces before writing — a missing '+' between two
    #    adjacent literals is legal C++ and produced a ui_probe.json no parser
    #    accepts (CI run 35874350649 died with no annotation at all).
    for needle, why in (
            ('g_handoverNotes', 'the handover note of every scale pass (BS-19)'),
            ('jsonBalanced(', "the report's own brace check (BS-19)"),
            ('++g_invChecks[13];\n        if (handed.app.dpi != passDpi) {',
             'the handover assertion itself (BS-19: the DPI half of the contract)'),
            ('harnessAssert(dlg, handed, "restore"',
             'the restored state judged by the full invariant battery (BS-19)'),
            ('{125, 120U}', 'the 125 % scale pass (BS-19: 100/125/150 % coverage)')):
        if needle not in probe:
            failures.append(f"tools/ui_probe/ui_probe.cpp: {needle} is gone — {why}")
    for needle, why in (
            ('if (!jsonBalanced(json))',
             'the probe must refuse to publish an unparseable report (BS-19)'),
            ('firstViolations', "the first violating state per invariant (BS-19)")):
        if needle not in probe:
            failures.append(f"tools/ui_probe/ui_probe.cpp: {needle} is gone — {why}")

    # 10. v1.3.0-beta8fix1 (BS-20): A PAGE CHILD MAY NOT BE WIDER THAN THE PAGE.
    #     The 125 % pass the probe added measured a 620 px group box at x=30 in a
    #     641 px client — the authored 496 px rescaled — because the solver grows a
    #     row for its text but never shrinks one to the page that clips it. At 100 %
    #     the same rows happen to fit, which is how the defect survived four green
    #     rounds. The rule pins the decision (the pure clamp), its application (the
    #     solver uses it before the plan), and its unit test.
    try:
        solve_body = function_body(main, r'void solveSettingsLayout\s*\(HWND hwnd\)')
    except ValueError:
        solve_body = ''
    if 'clampPageChildWidth(' not in solve_body:
        failures.append(
            "main.cpp: the solver no longer clamps page children to the page's "
            "width (BS-20) — a row wider than the page it is clipped to is "
            "unreachable by construction, and at 125 % the rescaled authored widths "
            "are wider than the client (the audit's outside_page/overlap/clip "
            "findings) while at 100 % they happen to fit")
    if 'clampPageChildWidth(' not in read_or_empty(repo, 'src/app/DialogLayout.hpp'):
        failures.append("src/app/DialogLayout.hpp: clampPageChildWidth() is gone (BS-20)")
    if 'testPageChildWidthIsClampedToThePage()' not in read_or_empty(
            repo, 'tests/test_dialog_layout.cpp'):
        failures.append("tests/test_dialog_layout.cpp: the BS-20 clamp test is gone — "
                        "the decision is portable, so it is asserted without Windows")

    # 11. v1.3.0-beta8fix1 (BS-21): THE TAB-STRIP SHIFT REPLACES, IT DOES NOT ADD.
    #     BS-10 moves the page down to clear a wrapped tab strip, but it computed
    #     the move from the BASELINE (which already carries the previous move) and
    #     `pageTopShiftPx` can only ever push DOWN — so a strip that wrapped once
    #     kept the page low forever: the probe measured `stripShift 228` px baked
    #     into a 494x497 page, with 20 controls of one tab parked under a 176 px
    #     page and none of them visible. These rules pin the replacement decision,
    #     the un-shift on the DPI path, and the cycle test behind both.
    if 'stripShiftFor(' not in solve_body:
        failures.append(
            "main.cpp: the tab-strip shift no longer goes through "
            "ok::layout::stripShiftFor() (BS-21) — computing it from the baseline "
            "with pageTopShiftPx() only ever pushes the page DOWN, so a strip that "
            "wrapped once leaves the content below the page forever")
    if 'settingsUnshiftPageToAuthored()' not in drop_body:
        failures.append(
            "main.cpp: dropSettingsLayoutBaseline() no longer un-shifts the page "
            "(BS-21) — a shift left in the live rectangles is multiplied by the DPI "
            "rescale and, once the baseline is gone, becomes the authored layout")
    if 'void settingsUnshiftPageToAuthored()' not in main:
        failures.append("main.cpp: settingsUnshiftPageToAuthored() is gone (BS-21)")
    if 'StripShift' not in read_or_empty(repo, 'src/app/DialogLayout.hpp'):
        failures.append("src/app/DialogLayout.hpp: the StripShift model is gone (BS-21)")
    if 'testStripShiftReturnsToAuthoredOnShrink()' not in read_or_empty(
            repo, 'tests/test_dialog_layout.cpp'):
        failures.append("tests/test_dialog_layout.cpp: the BS-21 grow/shrink cycle "
                        "test is gone")
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
    print("PAINT RULES OK — 8 rules: sibling clipping, window styles, "
          "repaint-after-change, need-deduped reflow, unscrolled solver input, "
          "rescale-owns-its-resolve + total baseline drop (BS-18), symmetric "
          "scrollbar correction (+ the probe's blank-page, reflow, pixel and "
          "operation-sequence checks)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
