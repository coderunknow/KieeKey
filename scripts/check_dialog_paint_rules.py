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


def struct_fields(source: str, name: str) -> list[str]:
    """Field names of `struct <name>` in declaration order (comments stripped).

    The probe carries a MIRROR of the app's scroll-state struct across the
    executable boundary (tools/ui_probe/ui_probe.cpp, main.cpp). The two are
    passed as one bare struct pointer, so a field added, removed or moved on one
    side only makes the other read another field's bytes: a `const char*` read out
    of an `int` is an access violation. That happened while BS-22c was being built
    — the probe died before it could write ui_probe.json and CI could only say
    "the UI probe did not write ui_probe.json". This helper is what lets the gate
    compare the two declarations field by field, in order.
    """
    stripped = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', source, flags=re.S)
    m = re.search(r'struct\s+' + re.escape(name) + r'\s*\{([^}]*)\}', stripped, re.S)
    if not m:
        return []
    out: list[str] = []
    for decl in m.group(1).split(';'):
        decl = decl.strip()
        if not decl:
            continue
        # `int a, b, c[9];` and `const char* p;` both carry several names per
        # declaration, so drop the leading type word and keep every name.
        head = decl.split('=')[0].strip()
        toks = head.split(None, 1)
        if len(toks) < 2:
            continue
        names = toks[1].replace('*', ' ')
        for part in names.split(','):
            name_part = part.strip().split('[')[0].strip()
            if not name_part:
                continue
            if len(name_part.split()) > 1:      # `char * latchWriter`
                name_part = name_part.split()[-1]
            out.append(name_part)
    return out


def blank_comments(source: str) -> str:
    """`source` with comment bodies blanked out, line numbers unchanged.

    A line scan has to see what the COMPILER sees: the code explains the pair it
    protects in prose, and prose that mentions `SetScrollInfo(SB_VERT)` is not a
    call site (the first version of the every-write-readopts rule matched the
    explanation and failed its own gate). String and character literals are
    blanked the same way so a `//` inside one cannot hide real code.
    """
    out: list[str] = []
    i, n = 0, len(source)
    while i < n:
        c = source[i]
        if c == '/' and i + 1 < n and source[i + 1] == '/':
            j = source.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and source[i + 1] == '*':
            j = source.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in source[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n:
                if source[j] == '\\':
                    j += 2
                    continue
                if source[j] == c or source[j] == '\n':
                    j += 1
                    break
                j += 1
            out.append(''.join(ch if ch == '\n' else ' ' for ch in source[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


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
        # v1.3.0-beta8fix1 (bug BS-22): the requirement is that the solver's input is
        # UN-SCROLLED. BS-17 met it by adding the offset back to the live rectangle
        # (solverInputRect) and preferring the saved baseline; BS-22 supersedes both
        # with the authored rectangle, which is un-scrolled by construction and also
        # un-shifted and un-grown (the additive-output defects of BS-21/BS-22). The
        # rule therefore accepts either mechanism and still fails on the bug it was
        # written for: taking the y straight from the scrolled live rectangle.
        uses_authored = 'authoredPageRect(' in solve
        uses_baseline = ('solverInputRect' in solve and
                         'g_settingsScroll.solved' in solve)
        if not (uses_authored or uses_baseline):
            failures.append(
                "main.cpp: solveSettingsLayout() no longer derives the page children's "
                "input from an un-scrolled source (the authored table, BS-22, or the "
                "saved baseline + solverInputRect, BS-17) — a reflow while the user is "
                "scrolled then folds the scroll offset into the layout and the page "
                "drifts away")
        if 'solverInputRect(spec.rect, g_settingsScroll.offset)' not in solve and \
                'solverInputRect(live, g_settingsScroll.offset)' not in solve:
            failures.append(
                "main.cpp: the solver no longer adds the scroll offset back to the "
                "live rectangle it captures the authored geometry from (BS-17/BS-22) "
                "— a control first seen while the page is scrolled would have the "
                "rendered position baked in as its authored one")
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
        # v1.3.0-beta8fix1 (bug BS-22c): the latch and the WS_VSCROLL bit are no
        # longer written here by hand — they have ONE owner
        # (settingsApplyScrollbarLatch), which the drop calls with `false`. The
        # requirement is unchanged: a dropped baseline may not keep a live scroll
        # state. Rule 15 pins the owner itself.
        for needle, what in (('g_settingsScroll.offset = 0;', 'the scroll position'),
                             ('g_settingsScroll.range = 0;', 'the scroll range'),
                             ('settingsApplyScrollbarLatch(g.hSettings, false,',
                              "the bar's latch and its WS_VSCROLL bit (through their "
                              "one owner)"),
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
    try:
        show_tab_body = function_body(main, r'void showTab\s*\(int tab\)')
    except ValueError:
        show_tab_body = ''
    try:
        scroll_body = function_body(main, r'void applySettingsScrollOffset\s*\(HWND hwnd\)')
    except ValueError:
        scroll_body = ''
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

    # 12. v1.3.0-beta8fix1 (BS-22): THE SOLVER'S INPUT IS THE AUTHORED GEOMETRY.
    #     Taking it from the live/baseline rectangles made every transform additive
    #     on its own previous output — the strip shift accumulated (BS-21), a row
    #     that wrapped at a narrow width kept its height AND the push it caused when
    #     the dialog widened again (the probe's "id 555 was at y=114, is at y=158
    #     after the strip fitted one row again"). The authored table (96-dpi px, so
    #     a rescale cannot skew it) makes a solve a pure function of the authored
    #     layout, the current text and the current width. These rules pin the table,
    #     its capture, the solver's use of it, and the cycle test behind them.
    for needle, why in (
            ('struct AuthoredPageRect', 'the authored-geometry table (BS-22)'),
            ('rememberAuthoredPageRect(', "the table's capture (BS-22)"),
            ('authoredPageRect(', "the table's lookup (BS-22)")):
        if needle not in main:
            failures.append(f"main.cpp: {needle} is gone — {why}")
    if 'authoredPageRect(id)' not in solve_body:
        failures.append(
            "main.cpp: the solver no longer reads the AUTHORED rectangle for page "
            "children (BS-22) — with the live/baseline rectangle as its input every "
            "transform is additive on its own previous output: the strip shift "
            "accumulates, and a row that wrapped at a narrow width keeps its height "
            "and its push when the dialog widens again")
    if 'testReflowIsRecomputedFromAuthored()' not in read_or_empty(
            repo, 'tests/test_dialog_layout.cpp'):
        failures.append("tests/test_dialog_layout.cpp: the BS-22 recomputed-reflow "
                        "test is gone")

    # 13. v1.3.0-beta8fix1 (bug BS-22, part 2): THE ROW IS MEASURED AT THE WIDTH IT
    #     GETS. `requiredHeight` answers one question — how tall does this label need
    #     to be in the box it is about to get — and BS-20 narrows every page child to
    #     the dialog's own right edge. Measured first, clamped second, a row is grown
    #     for the lines that fit the wide width and laid out with the lines that fit
    #     the narrow one: the extra line is clipped. The probe says it in those exact
    #     numbers — `[I12] id 561 needs 352px but its box is 320px tall` at dpi 144.
    #     The order is the fix, so the order is what this rule pins.
    i_clamp = solve_body.find('clampPageChildWidth(')
    i_measure = solve_body.find('requiredHeight = measureStaticTextHeightPx(')
    if i_clamp < 0:
        failures.append("main.cpp: the solver no longer clamps a page child to the "
                        "page's width (BS-20/BS-22)")
    elif i_measure < 0:
        failures.append("main.cpp: the solver no longer measures a growable row's "
                        "required height (BS-22)")
    elif i_measure < i_clamp:
        failures.append(
            "main.cpp: the solver measures a growable row's required height BEFORE "
            "clamping its width (BS-22) — the count it grows for belongs to a box the "
            "row will not get, so at a narrowed page a line of text has no room and "
            "is clipped (the probe's I12: 'id 561 needs 352px but its box is 320px "
            "tall'). Clamp first, measure second")
    if 'testGrowableIsMeasuredAtTheWidthItGets()' not in read_or_empty(
            repo, 'tests/test_dialog_layout.cpp'):
        failures.append("tests/test_dialog_layout.cpp: the BS-22 measure-at-the-final-"
                        "width test is gone")

    # 14. v1.3.0-beta8fix1 (bug BS-22, part 3): THE PROBE'S DERIVED EXPECTATIONS.
    #     Two of the harness's assertions were calibrated for the OLD inputs and
    #     became wrong when the solver started from the authored geometry — both
    #     fired on states the app was right about (309 + 81 of the 76f955a run):
    #       * the strip-shift record is now the ABSOLUTE distance from the authored
    #         top to the display rectangle, and the dialog's one-row display top is
    #         14 px (96 dpi) below the authored first row (group boxes at y=100, the
    #         display rectangle at 114). "the strip worked, so the shift is 0" called
    #         that a defect. What must never happen is a value that grows and STAYS
    #         grown, so the cycle compares the shift with the state it started from —
    #         and the live geometry (page top, first row, full invariant battery) is
    #         asserted separately, which is what keeps the comparison strict.
    #       * the Win32 range is built from ONE tab's depth (settingsScrollSetTab:
    #         nMax/nPage from perTabContentBottom[tabIndex]), while WS_VSCROLL is the
    #         all-tabs decision. Asking the usable/dead answer about all nine depths
    #         made the check contradict itself and I4 (which verifies the range
    #         against the CURRENT tab). The ruler is the current tab.
    if ('back.app.stripShift[tab] != baseShift &&' not in probe or
            'const int baseShift = base.app.stripShift[tab];' not in probe):
        failures.append(
            "tools/ui_probe/ui_probe.cpp: the strip cycle no longer compares the "
            "shift with the value it started from (BS-22/BS-22h) — with the authored "
            "input the one-row shift is the display rectangle's inset (14 px at "
            "96 dpi, by design: the content starts at the display rect), so a `!= 0` "
            "test fails on a correct dialog, while the accumulation this contract "
            "exists for is a value that does not come back; the comparison base is "
            "the displayed height the cycle recorded at 100 %")
    # 17. v1.3.0-beta8fix1 (bugs BS-22d / BS-23): TWO MORE MEASURED STATES.
    #     * BS-22d — the tab strip's row count is a layout change in BOTH
    #       directions. The grow direction forced a re-layout since BS-10; the
    #       shrink direction did not, so TCM_ADJUSTRECT kept answering with the
    #       two-row display rectangle and the solver pushed the page down by a row
    #       for a strip that was one row tall (87 x `[I1] the page top did not
    #       return to its one-row value: 114 -> 130 (wrapped) -> 130`, with the
    #       page 16 px low the controls the app believes are visible sit under the
    #       strip's second row: `[I11] the page paints background only`).
    #     * BS-23 — a row may GROW IN WIDTH to fit its own text, bounded by the
    #       page, the nearest sibling and the enclosing group box. The last `clip`
    #       finding of the settled audit is that bound missing: 16 px of Vietnamese
    #       label in a box the authored width scales 16 px too small for.
    if 'stripRows != g_settingsScroll.stripRows' not in solve_body:
        failures.append(
            "main.cpp: the solver stops re-laying the tab strip out when its row "
            "count changes (BS-22d) — TCM_ADJUSTRECT keeps answering with the display "
            "rectangle of the strip that was there BEFORE the change, so a strip that "
            "went back to one row still pushes the whole page down by a row and every "
            "control the app believes is visible hides under the tab labels")
    if '~static_cast<LONG_PTR>(TCS_MULTILINE)' not in solve_body:
        failures.append(
            "main.cpp: the tab control's TCS_MULTILINE style is never CLEARED again "
            "(BS-22d) — the bit tracks the planned row count, and it is what makes "
            "the control re-lay its rows out; leaving it set after the labels fit one "
            "row again keeps the two-row display rectangle in force, so the page is "
            "pushed down under a strip that is one row tall (87 x `[I1] the page top "
            "did not return to its one-row value`)")
    if 'tabH - 1' not in solve_body:
        failures.append(
            "main.cpp: the size-change nudge that makes the tab control re-lay out "
            "after it goes back to one row is gone (BS-22d) — a size change is the "
            "one event the control is guaranteed to re-lay its rows out for")
    if '::UpdateWindow(tabCtl)' not in solve_body:
        failures.append(
            "main.cpp: the forced tab re-layout is gone (BS-10/BS-22d) — the style "
            "bit and TCM_ADJUSTRECT disagree until the control processes the change")
    if 'measureSingleLineWidthPx(' not in main:
        failures.append("main.cpp: the single-line width measurement is gone (BS-23) — "
                        "the solver cannot fit a row to its text without it")
    if 'spec.requiredWidth = measureSingleLineWidthPx(' not in solve_body:
        failures.append(
            "main.cpp: the solver no longer measures a single-line row's width "
            "(BS-23) — the dialog's labels are authored at 96 dpi and the fonts are "
            "not, so at 125 % a check box label 16 px wider than its box loses the "
            "end of the sentence (the audit's last `clip` finding)")
    if 'disp.bottom, static_cast<int>(disp.right))' not in solve_body:
        failures.append(
            "main.cpp: autoFit() is no longer given the page's right edge (BS-23) — "
            "without it the width fit has no page bound and a row could grow past "
            "the clip rectangle it is drawn in")
    header = read_or_empty(repo, 'src/app/DialogLayout.hpp')
    for needle, why in (
            ('int requiredWidth = 0;', "the measured single-line width (BS-23)"),
            ('int pageRightPx = 0', "the page bound of the width fit (BS-23)"),
            ('kRowGapPx', "the gap the width fit keeps to the next row (BS-23)"),
            ('limit = std::min(limit, orc.x - kRowGapPx);',
             "the sibling bound of the width fit (BS-23)")):
        if needle not in header:
            failures.append(f"src/app/DialogLayout.hpp: {needle} is gone — {why}")
    if 'testRowWidthFitsItsTextWithinThePage()' not in read_or_empty(
            repo, 'tests/test_dialog_layout.cpp'):
        failures.append("tests/test_dialog_layout.cpp: the BS-23 width-fit test is gone")

    # 16. v1.3.0-beta8fix1 (bug BS-22c): THE PROBE'S STATE MIRROR MATCHES THE APP'S.
    #     Two TUs on opposite sides of an extern "C" pointer, one struct: the two
    #     declarations have to agree field for field, in order. A mismatch is not a
    #     finding, it is a crash (the probe read `int viewportX` as a `const char*`
    #     and died before writing its report). The app side answers with
    #     KieeKeyProbeScrollStateSize(); this rule catches the drift before a
    #     Windows runner is spent on it.
    app_state = struct_fields(main, 'KieeKeyProbeScrollStateT')
    probe_state = struct_fields(probe, 'ProbeScrollStateT')
    if not app_state or not probe_state:
        failures.append(
            "main.cpp / tools/ui_probe/ui_probe.cpp: the probe's scroll-state struct "
            "or its mirror could not be read (BS-22c) — the gate that keeps the two "
            "in step is blind if either declaration is renamed")
    elif app_state != probe_state:
        failures.append(
            "the probe's ProbeScrollStateT does not mirror the app's "
            "KieeKeyProbeScrollStateT field for field (BS-22c): app=[" +
            ", ".join(app_state) + "] probe=[" + ", ".join(probe_state) + "] — the "
            "struct crosses an extern \"C\" boundary by pointer, so a difference "
            "reads one field as another (a crash, not a finding)")

    # 15. v1.3.0-beta8fix1 (bug BS-22c): FOUR MECHANISMS FROM THE 77e8fea RUN.
    #     Each one is a state the probe measured, and each one has one owner:
    #       * the page clamp may NARROW a row, never widen it. Its first version
    #         raised every row thinner than S(80) to that minimum, so "VNI" (58 px)
    #         became 100 px at 125 % and ran into "Simple Telex" (`overlap by
    #         15x25 px`), and "Từ điển" (68 px) stuck 34 px out of the page
    #         (`outside_page`). A width is authored geometry.
    #       * a combo box's height is WIN32's: it sizes its closed window to its
    #         item height, and re-sizes it on every font change. Imposing the
    #         authored height left the live window a few px taller than the solved
    #         baseline — the probe's I6 (a 2 px sliver inside the viewport, region
    #         EMPTY) and the combo/static overlaps of the row below it.
    #       * a control's REGION is computed from the rectangle the window actually
    #         has, not from the baseline: a region derived from a rectangle that is
    #         2 px shorter clips a visible control to nothing. Same I6, structurally
    #         impossible after this.
    #       * the bar latch and WS_VSCROLL are ONE answer with ONE writer
    #         (settingsApplyScrollbarLatch), re-decided by settingsSyncScrollbarLatch
    #         whenever an operation changes which depth the range describes — the
    #         probe's I5, 58 x "the app's bar latch says on but WS_VSCROLL is clear".
    if 'clampPageChildWidth(spec.rect, clampLimitRight)' not in solve_body:
        failures.append(
            "main.cpp: the page clamp call changed shape (BS-22c) — the solver "
            "clamps a page child to the page's right edge with no minimum width: a "
            "minimum WIDENS rows the author meant to be narrow, which is how "
            "\"VNI\" (58 px) reached 100 px at 125 % and ran into its neighbour")
    clamp_body = ''
    try:
        clamp_body = function_body(read_or_empty(repo, 'src/app/DialogLayout.hpp'),
                                   r'Rect clampPageChildWidth\s*\([^)]*\)\s*noexcept')
    except ValueError:
        failures.append("src/app/DialogLayout.hpp: clampPageChildWidth() not found "
                        "(BS-20/BS-22c)")
    if clamp_body and 'minWidthPx' in clamp_body:
        failures.append(
            "src/app/DialogLayout.hpp: clampPageChildWidth() carries a minimum "
            "width again (BS-22c) — a floor that runs on every row WIDENS the "
            "deliberately narrow ones (radio buttons, small check boxes) into their "
            "neighbours, which the probe reports as `overlap`, and pushes the ones "
            "at the right edge out of the page (`outside_page`)")
    if 'if (isCombo && liveH > 0)' not in solve_body:
        failures.append(
            "main.cpp: the solver imposes its own height on a COMBO BOX again "
            "(BS-22c) — Win32 sizes a closed combo to its item height and re-sizes "
            "it on every font change, so the live window ends up taller than the "
            "solved baseline: the region drawn from the baseline clips a visible "
            "control to nothing (I6) and the row below is placed under the window "
            "the combo really has (overlap)")
    if ('settingsApplyScrollbarLatch(' not in solve_body or
            'SetWindowLongPtrW(hwnd, GWL_STYLE' in solve_body):
        failures.append(
            "main.cpp: the solver writes the bar latch / WS_VSCROLL pair by hand "
            "again (BS-22c) — the two are ONE answer and only "
            "settingsApplyScrollbarLatch() may write them; written apart they "
            "diverge, and the user is left with a scrollbar the app believes in or "
            "a page with no way to reach its content (I5)")
    if 'settingsApplyScrollbarLatch(' not in drop_body:
        failures.append(
            "main.cpp: dropSettingsLayoutBaseline() no longer drops the bar pair "
            "through its one owner (BS-22c) — a dropped baseline with a live latch "
            "is a scroll state that describes a layout that no longer exists")
    if 'settingsApplyScrollbarLatch(hwnd, have, "win32-visibility")' not in main:
        failures.append(
            "main.cpp: the bar latch is no longer re-adopted from the window after "
            "SetScrollInfo (BS-22c) — Windows hides a standard scroll bar when the "
            "range says nothing is left to scroll (per tab!) and hiding one clears "
            "WS_VSCROLL, so the latch left behind describes a window that no longer "
            "exists")
    # The unconditional half of the same rule: the bit belongs to Windows, so
    # EVERY write of the dialog's scroll state is followed by the re-adopt. A new
    # SetScrollInfo added without one is how the pair diverges again (I5), and the
    # divergence is invisible in every rectangle the app holds.
    main_lines = blank_comments(main).splitlines()
    for index, line in enumerate(main_lines):
        if 'SetScrollInfo(' not in line or 'SB_VERT' not in line:
            continue
        # Comments are blank by now, so the window may skip over the prose that
        # explains the call it is looking for.
        window = [l for l in main_lines[index + 1:index + 6] if l.strip()]
        if not window or 'settingsAdoptScrollbarVisibility(' not in window[0]:
            failures.append(
                f"main.cpp:{index + 1}: SetScrollInfo(SB_VERT) is not followed by "
                "settingsAdoptScrollbarVisibility() (BS-22c) — SetScrollInfo can "
                "hide the standard bar and clear WS_VSCROLL behind the app's back "
                "(the range is per tab), so every write must re-read the bit into "
                "the latch before the next solve or probe observation")
    # v1.3.0-beta8fix1 (bug BS-22f): the font factory must answer a request with
    # the face that was asked for, and the text-scale entry point must know every
    # face the dialog can be wearing. Both were "unreachable" assumptions: the
    # factory had 8 slots for the 9 faces three scale factors need (and 36 for the
    # probe's own dpi x scale walk), and its full-cache fallback handed back the
    # first face it ever created — so the app's 120 dpi faces were 13 px at 96 dpi
    # and the probe's 150 % faces were the same OBJECT as the 100 % ones.
    try:
        font_body = function_body(read_or_empty(repo, 'src/app/main.cpp'),
                                  r'HFONT cachedFont\s*\([^)]*\)\s*noexcept')
    except ValueError:
        failures.append("main.cpp: cachedFont() not found (BS-22f) — the dialog's "
                        "faces come from it and the layout measures text with the "
                        "font a control is actually wearing")
        font_body = ''
    if font_body:
        if 'cache.slots[0].font' in font_body:
            failures.append(
                "main.cpp: cachedFont() answers a face it does not have with "
                "another face (BS-22f) — layout measures text with the control's "
                "LIVE font, so a request answered with a face of another "
                "dpi/size/weight silently sizes rows for the wrong text; when the "
                "ninth face was asked for and the cache had eight slots, every "
                "later face came back 13 px at 96 dpi and the 150 % tab labels "
                "stopped widening one dpi switch into the probe's walk (54 x I1)")
        after_create = font_body.split('CreateFontW(', 1)[1] if 'CreateFontW(' in font_body else ''
        if not after_create:
            failures.append("main.cpp: cachedFont() no longer mints a face at all "
                            "(BS-22f)")
        else:
            if 'if (freeSlot != FontCache::kSlots)' not in after_create:
                failures.append(
                    "main.cpp: cachedFont() no longer hands back the face it just "
                    "minted when the cache has no room for it (BS-22f) — the "
                    "freshly created face is the answer; the slot is bookkeeping")
            if 'for (std::size_t i = 0; i < FontCache::kSlots; ++i)' not in after_create:
                failures.append(
                    "main.cpp: the out-of-GDI-handles fallback of cachedFont() is "
                    "no longer a bounded walk of the cache (BS-22f)")
        slots = re.search(r'kSlots\s*=\s*(\d+)', main)
        if slots is None:
            failures.append("main.cpp: FontCache::kSlots not found (BS-22f)")
        elif int(slots.group(1)) < 36:
            failures.append(
                f"main.cpp: the font cache holds {slots.group(1)} faces (BS-22f) — "
                "one session needs three faces per scale factor (13 normal, 13 "
                "semibold, 20 semibold), and the UI probe walks four dpis "
                "(96/120/144/192) at text scales 100/125/150 %, i.e. 4 x (3 + 3) = "
                "36 distinct faces; a full cache is what made every later face "
                "come back 13 px at 96 dpi")
    try:
        scale_body = function_body(read_or_empty(repo, 'src/app/main.cpp'),
                                   r'int\s+KieeKeyProbeFontScale\s*\([^)]*\)')
    except ValueError:
        failures.append("main.cpp: KieeKeyProbeFontScale() not found (BS-22f) — the "
                        "probe's text-scale path is how the strip transition is "
                        "driven")
        scale_body = ''
    if scale_body:
        for needle, why in (
                ('ctx.add(app[role], next[role]);',
                 "the app's own faces at the CURRENT dpi"),
                ('ctx.add(g_probeFontHistory[role][i], next[role]);',
                 'the faces this probe applied earlier (a dpi change re-applies the '
                 "app's faces, a scale change leaves the probe's)")):
            if needle not in scale_body:
                failures.append(
                    f"main.cpp: the probe's text scale no longer maps {why} "
                    "(BS-22f) — the scale then matches nothing (the labels never "
                    "widen: the strip never wraps and the transition is measured in "
                    "a state that never reached it) or maps the wrong way (a 100 % "
                    "call cannot undo a 150 % one)")
    if 'settingsSyncScrollbarLatch(' not in show_tab_body:
        failures.append(
            "main.cpp: showTab() no longer re-decides the bar (BS-22c) — switching "
            "tabs changes which depth the range describes, and the probe caught "
            "exactly that operation with the latch and the style bit disagreeing "
            "(`op select_tab`, 58 findings in the 77e8fea run)")
    if ('GetWindowRect(entry.first, &live)' not in scroll_body or
            'shown = ok::layout::scrollChildRect(' not in scroll_body):
        failures.append(
            "main.cpp: applySettingsScrollOffset() computes a control's region from "
            "the solved baseline again (BS-22c) — the baseline can be a few pixels "
            "shorter than the window Win32 actually gave back (a combo box sizes "
            "itself), and the region of a partly visible control then says \"fully "
            "outside\" and paints nothing where the user should see a sliver (I6)")
    for needle, why in (
            ('settingsApplyScrollbarLatch(', 'the ONE writer of the bar pair (BS-22c)'),
            ('settingsSyncScrollbarLatch(', 'the re-decision of the bar pair (BS-22c)')):
        if needle not in main:
            failures.append(f"main.cpp: {needle} is gone — {why}")
    # v1.3.0-beta8fix1 (bug BS-22j): A LABEL THAT DOES NOT FIT ITS BOX MUST BE
    # ABLE TO WRAP. A page BUTTON's label is authored to fit one line at 100 %,
    # and Win32 paints a button on ONE line unless BS_MULTILINE says otherwise —
    # so at a bigger text scale (or in a narrower window) the tail of the label
    # is clipped with no ellipsis and no way to reach it. The x64 run 35974677491
    # measured seven of them; the app's own measurement of one of those rows said
    # 64 px where the plan had put a 25 px box. The fix has two halves that must
    # both be present, so both are nailed here: mkCtl gives every page BUTTON the
    # bit, and the solver grows the row to the wrapped height measured in the
    # width the row will really have (after the clamp, minus the button's own
    # text inset).
    for needle, why in (
            ('style |= BS_MULTILINE;',
             'the button style that lets Windows wrap a page label (BS-22j) — '
             'without it the grown row paints one clipped line and the height is '
             'wasted'),
            ('settingsPageOf(static_cast<int>(reinterpret_cast<INT_PTR>(id))) !=',
             'the page/chrome split that decides WHICH buttons may wrap (BS-22j) — '
             'the always-visible row keeps its fixed band and its one-line labels'),
            ('const bool pageButton = isButton && !isGroupBox &&',
             'the row class the solver must treat as growable (BS-22j)'),
            ('const int buttonTextPad = pageButton ? (checkLike ? S(24) : S(12)) : 0;',
             "the inset Windows keeps for the button's own glyph, so the label is "
             'measured in the width it will really wrap in (BS-22j)'),
            ('? std::max(1, static_cast<int>(spec.rect.w) - buttonTextPad)',
             'the wrapped height measured in the row WIDTH the plan will apply — '
             'after the clamp (BS-22b), which is the whole point (BS-22j)'),
            ('((style & SS_TYPEMASK) != SS_OWNERDRAW)) || pageButton;',
             'the growable flag that lets autoFit give the wrapping button its '
             'measured height (BS-22j)')):
        if needle not in main:
            failures.append(f"main.cpp: {needle} is gone — {why}")
    # v1.3.0-beta8fix1 (bug BS-22l): A WINDOW CLASS IS COMPARED AS A NAME.
    # `clsLen == 6 && lstrcmpiW(cls, L"COMBOBOX")` is the bug that hid the combo
    # branch for two rounds: Win32's name for a combo is "ComboBox", eight
    # characters, so the guard was never true. The solver's comparisons are by
    # name now, and the length test may not come back (comments are blanked here,
    # so the note above does not count as code).
    for needle, why in (
            ('const bool isCombo = ::lstrcmpiW(cls, L"COMBOBOX") == 0;',
             "the combo branch's own test — Win32's class name for a combo is "
             '"ComboBox" (8 characters), and the length-guarded version could never '
             'be true, which left every combo planned at its authored height while '
             'Win32 gave it its own (id 504: plan 25, window 33)'),
            ('const bool isButton = ::lstrcmpiW(cls, L"BUTTON") == 0;',
             'the same comparison for buttons, without a length to get wrong'),
            ('const bool isStatic = ::lstrcmpiW(cls, L"STATIC") == 0;',
             'and for statics')):
        if needle not in main:
            failures.append(f"main.cpp: {needle} is gone — {why}")
    if 'clsLen == 6' in blank_comments(main):
        failures.append(
            "main.cpp: a window class is compared by its LENGTH again "
            "(`clsLen == 6 && lstrcmpiW(...)`) (BS-22l) — the predefined classes "
            'are "Static"/"Button" (6) but "ComboBox" is EIGHT, so the guard is '
            'false exactly where it matters and the branch it guards is dead code')
    # v1.3.0-beta8fix1 (bug BS-22l): THE HEIGHT THE WINDOW HAS IS PART OF THE PLAN.
    # A combo box sizes its own window; taking that height but dropping the rows
    # below it is what the 35980164209 run measured twice — `[I6] id 504 lives
    # 128,297 210x33 but the solver's baseline is 128,297 210x25` and `[overlap]
    # id 625 (ComboBox) ... and id 627 (Static) ... overlap by 398x6 px`.
    # v1.3.0-beta8fix1 (bug BS-22n): A SIZE THE COMBO DECIDED IS A REFLOW REQUEST.
    # Its live height changes again AFTER the solve (font change, theme change), and
    # the plan then describes a window that does not exist — the 35981669220 run
    # measured it 306 times, all combos, both signs (`lives 330x36 but the solver's
    # baseline is 330x38`). The control's own notification has to ask for a reflow.
    for needle, why in (
            ('if (msg == WM_WINDOWPOSCHANGED && !g_settingsReflowPosted && '
             'g_settingsSolveDepth == 0) {',
             'the combo subclass asking for a reflow when the window it lives in '
             'resized itself (BS-22n) — posted, guarded against re-entry, and only '
             'when the app is not solving'),
            ('::PostMessageW(dlg, WM_APP + 78, 0, 0);',
             'the posted reflow request itself (BS-22n)'),
            ('case WM_APP + 78:', 'the handler for that request (BS-22n)'),
            ('bool settingsWindowFitsPlan(HWND child) noexcept {',
             'the question the subclass asks — the live SIZE against the planned '
             'rectangle (BS-22n); a move is not a resize and is ignored'),
            ('const SettingsSolveScope solvingHere;',
             'the scope guard that keeps the solver\'s own SetWindowPos calls from '
             'looking like a control resizing itself (BS-22n)')):
        if needle not in blank_comments(main):
            failures.append(f"main.cpp: {needle} is gone — {why}")
    for needle, why in (
            ('if (static_cast<int>(live.right - live.left) != plan.rects[i].w ||',
             'the check that a window took the size it was given (BS-22l) — a '
             'combo box answers SetWindowPos with its own height, and a plan that '
             'describes a window that does not exist is what put the row below it '
             'inside the combo (`[overlap] ... overlap by 398x6 px`)'),
            ('const LRESULT rows = ::SendMessageW(tabCtl, TCM_GETROWCOUNT, 0, 0);',
             "the app reading the tab control's OWN row count (BS-22o) — the plan's "
             '`multiline` flag is an intention (Win32 can keep nine narrow labels in '
             'one row even with TCS_MULTILINE set) and the display rectangle moves for '
             'a taller single row too, so neither may stand in for "the labels '
             'wrapped"'),
            ('++g_settingsSolvePass;',
             'the bounded second solve that reads the height the window really has '
             '(BS-22l) — the same guard the BS-14 width pass uses, so this is one '
             'extra pass and never a loop')):
        if needle not in blank_comments(main):
            failures.append(f"main.cpp: {needle} is gone — {why}")
    if 'spec.liveHeight = liveH;' not in main:
        failures.append(
            "main.cpp: the combo box's real height is no longer handed to the "
            "solver (BS-22l) — Win32 sizes a CBS_DROPDOWNLIST to its item height "
            'and its borders, so the authored 25 px is not the window ("the plan '
            'and the window disagree")')
    if 'spec.rect.h = liveH;' in blank_comments(main):
        failures.append(
            "main.cpp: the live height is written into the AUTHORED rectangle again "
            "(BS-22l) — the solve must stay a pure function of the authored layout "
            "(BS-22), and the design wants the authored box plus a measured height: "
            'ControlSpec::liveHeight exists for exactly that')
    if 'if (c.liveHeight > 0) { want = c.liveHeight; }' not in layout:
        failures.append(
            "src/app/DialogLayout.hpp: the plan no longer takes the height the WINDOW "
            "has (BS-22l/BS-22p) — a combo's real box then describes a window that "
            "does not exist, and the row below it is placed inside it. The window's "
            "measurement wins in BOTH directions: taking it only when it was larger "
            "left the plan at the authored height for a window that had got shorter "
            "(35983713630: `id 596 lives 420,550 330x36 but the solver's baseline is "
            "420,550 330x38`, 306 states)")
    if 'plan.rects[i].h = want;' not in layout:
        failures.append(
            "src/app/DialogLayout.hpp: the plan no longer carries the height it "
            "decided (BS-22l/BS-22p)")
    if 'std::vector<int> growth(controls.size(), 0);' not in layout:
        failures.append(
            "src/app/DialogLayout.hpp: the growth pass (the shift pass's input) is gone "
            "(BS-22l) — growth is what moves the rows below a control that takes more "
            "space than the authored table gave it")
    for test in ('testRegionFollowsTheLiveRectangleNotTheBaseline()',
                 'testPageChildWidthIsClampedToThePage()',
                 'testLiveHeightIsPlannedAndPushesTheRowsBelow()'):
        if test not in read_or_empty(repo, 'tests/test_dialog_layout.cpp'):
            failures.append(f"tests/test_dialog_layout.cpp: {test} is gone (BS-22c)")
    # v1.3.0-beta8fix1 (bug BS-22c): the strip cycle must REACH the transition it
    # judges. The first version narrowed the client to 55 % to make the tab labels
    # wrap — a property of the label font, which did not wrap at 125 %/150 % — so
    # the cycle could "pass" without ever growing the strip. The wrap is now driven
    # by the harness's text-scale path (1.5x at every scale) and the app's own
    # record of how deep the shift went (`stripSeen`) is what proves it.
    for needle, why in (
            ('kWrapFontPct', 'the text-scale driver of the strip cycle (BS-22c)'),
            ('KieeKeyProbeFontScale(dlg, kWrapFontPct)',
             'the wrap that makes the cycle measurable (BS-22c)'),
            ('wrapGrow <= 0',
             'the proof that the strip really grew with the labels (BS-22h) — the '
             "app's stripShift comes from the tab control's display rectangle, so a "
             'cycle that cannot grow it is measuring a strip that did not move'),
            ('const int wrapGrow = wrapped.app.stripShift[tab] - baseShift;',
             "the app's own displayed strip height as the comparison base "
             '(BS-22h)')):
        if needle not in probe:
            failures.append(f"tools/ui_probe/ui_probe.cpp: {needle} is gone — {why}")
    # v1.3.0-beta8fix1 (bug BS-22g/BS-22h): A MEASUREMENT MUST PROVE ITS
    # PRECONDITION. Three checks were green-able (or red-able) in states where they
    # measured nothing: the strip cycle ran from a strip that was already wrapped
    # (54 I1 findings about a transition that had happened) and then from a client
    # so wide that 150 % still fitted one row (81 of them), the screen-paint check
    # counted controls its capture did not cover as "painted nothing" (2 I11), and
    # neither finding named the rectangle the app's own solver had applied.
    for needle, why in (
            ('::MulDiv(wideW, static_cast<int>(passDpi), 96)',
             'the dpi-scaled client each pass starts from (BS-22g)'),
            ('measurable = probeState.app.stripRows > 1;',
             'the calibration that finds a client where the SAME labels wrap at the '
             'wrap font (BS-22o) — the plan\'s row count is the only signal that means '
             '"the labels do not fit": a 1.5x font also makes a single row taller and '
             'moves the display rectangle without any wrap (35982159995 measured '
             '`page top 92 -> 100` with `rows 1`)'),
            ('stripW += stripW / 4;',
             'the widen step of that search, for the scale where the labels do not fit '
             'one row even at 100 % (BS-22o)'),
            ('stripW = std::max(wideW, stripW * 4 / 5);   // still one row: narrower',
             'the narrow step, for a client so wide that 150 % still fits (BS-22o)'),
            ('const bool wrappedRows = wrapped.app.stripRows > 1;',
             'the wrap proof itself — the per-tab cycle judges the app\'s own row count '
             'as well as the page moving (BS-22o)'),
            ('!wrappedRows || wrapGrow <= 0 || wrapped.page.top < basePageTop)) {',
             'the cycle refusing to report green when the labels did not wrap or the '
             'page did not pay for it (BS-22o/BS-22c)'),
            ('back.app.stripShift[tab] != baseShift',
             'the shrink direction compared with the same displayed height the '
             'cycle started from (BS-22h)'),
            ('if (mine.empty()) { ++uncovered; continue; }',
             'the screen-paint check skipping a control its capture does not cover '
             '(a control off the captured frame is not evidence of a blank page) '
             '(BS-22g/BS-22m — the samples are now kept per control so the same '
             'points can be read on the post-repaint frame too)'),
            ('const bool pagePaintedOnScreen = bareInCapture && (bareScreen != bg);',
             'the measurement that tells "the page painted and its content is missing" '
             'from "the page area is the window\'s background" (BS-22q) — a point '
             'inside the page but outside every control, read on both frames'),
            ("the page area is the WINDOW's background",
             'the finding that names the second state (BS-22q)'),
            ('KieeKeyProbeSimulateDpi(dlg, nativeDpi);\n    KieeKeyProbeFontScale(dlg, 100);\n'
             '    KieeKeyProbeResize(dlg, static_cast<int>(origClient.right),\n'
             '                       static_cast<int>(origClient.bottom));\n'
             '    KieeKeyProbeReflowNow(dlg);\n'
             '    KieeKeyProbeSetOffset(dlg, 0);\n'
             '    KieeKeyProbeSelectTab(dlg, deepestTab);',
             'the restore that puts the display-change scenario back at the state this '
             'pass audits before it captures (BS-22q) — clearing the dpi override does '
             'not notify the dialog, so the capture used to measure a 96-dpi window '
             'the app believed was 144'),
            ('paintedAfter > 0) {',
             'the screen-paint finding telling a frame that never showed the content '
             'from one that showed it only after RedrawWindow() (BS-22m) — 35981669220 '
             'reported `the page paints background only` for a state where the app\'s '
             'own repaint put every one of those rows on the screen, so the two states '
             'have to be told apart by measurement'),
            ('if (judged < 3) {',
             'the screen-paint check refusing to report green when it could not '
             'judge three whole controls (BS-22g)'),
            ('const int renderedPts = renderPaintCountAt(dlg, samples, bg);',
             "the screen-paint finding's cross-check against the frame the app would "
             'draw (WM_PRINTCLIENT): a capture that reads as background everywhere is '
             'either a blank page or somebody else\'s pixels, and only the render '
             'tells the two apart (BS-22g)'),
            ('if (passDpi == nativeDpi) {',
             'the rule that the strip transition MUST be measurable at the '
             "runner's own scale (BS-22i) — the other passes may only report "
             'themselves unavailable, and the native one may not'),
            ('++g_stripCycleUnavailable;',
             'the counted, reported unavailability of a scale the screen cannot '
             'give room to (BS-22i)'),
            ('if (measurable) {',
             'the guard that runs the full invariant battery on the wrapped state '
             'even where the height growth is not measurable (BS-22i)'),
            ('for (HWND c = ::GetWindow(dlg, GW_CHILD); c != nullptr && chromeJudged < 4;',
             'the chrome half of the screen-paint evidence read from the DIALOG\'s '
             'children (BS-22k) — asking the page\'s control list which of its '
             'controls is always-visible never finds one, so `chrome 0/0` was the '
             'only value that check could ever print'),
            ('judged >= 3 && painted == 0 && renderedPts > 0',
             'the rule that decides whose frame the capture is (BS-22k): the app\'s '
             'own render paints text, the capture has no ink on the page AND none on '
             'the always-visible chrome — that is not this window\'s frame, it is a '
             'reported unavailable capture, not a blank page'),
            ('++g_screenUnavailable;\n                harnessTrace(',
             'the counted, traced record of that state (BS-22k) — an unusable capture '
             'must be visible in the digest, never silent'),
            ('checkPlanHeld(a, ctls);',
             'the pass audit\'s half of the plan-vs-window check (BS-22k) — the '
             'harness sees the states it drives, the pass audit sees the pass\'s own '
             'client and every scroll offset'),
            ('KieeKeyProbeSolvedRect(dlg, c.id, solved)',
             'the harness half of that check: the window against the rectangle the '
             'solver applied, in every state, with both rectangles named (BS-22k)'),
            ('(isButton && (style & BS_MULTILINE) != 0);',
             "the probe's growable inference for page buttons (BS-22j) — the solver "
             'grows them, so the app\'s own measurement is a contract for them and '
             'I12 must judge them (a check that skips the rows the clip class was '
             'made of is a check that never fires)'),
            ('const int clientH = static_cast<int>(origClient.bottom);',
             "the strip cycle's client HEIGHT is the pass's own pixel height "
             '(BS-22m, corrected by BS-22o): scaling it to the dpi asked a 1024x768 '
             'runner for a window taller than its display, and 35982159995 measured '
             'the result — `page 19,161 645x-11` with every child region-clipped to '
             '`0x0`, 74 states'),
            ('const bool baseOneRow = baseState.app.stripRows <= 1;',
             'the calibration proving the ONE-ROW half of the transition before the '
             'wrap half is allowed to count (BS-22o) — a scale where one row is '
             'unreachable is reported as an unavailable transition instead of being '
             'skipped tab by tab'),
            ('const bool baseOneRowFinal = baseFinal.app.stripRows <= 1;',
             'the SAME precondition judged on the state the cycles really run in, '
             'after the app\'s own refit decided the width it keeps (BS-22o, part two): '
             'a widened client the app shrinks back proves a precondition the cycle '
             'never gets, which is how 35985183906 reported `strip shift 29 -> 29 px` '
             'at 27 states'),
            ('if (!baseOneRowFinal) { measurable = false; }',
             'the rule that both halves of the transition have to hold on that state '
             '(BS-22o, part two)'),

            ('std::string tabState = "tab n/a";',
             'the tab control\'s own state (rectangle, visibility, region, row count) '
             'recorded next to the screen-paint finding (BS-22q) — "the tab control did '
             'not paint" and "the tab control is not there" are different repairs, and '
             'the numbers have to say which one it is'),
            ('int chromePainted = 0;',
             'the chrome half of that evidence (BS-22h) — a capture with the chrome '
             'and not the page is a page that was never painted, while one with '
             'neither is not this window\'s frame at all')):
        if needle not in probe:
            failures.append(f"tools/ui_probe/ui_probe.cpp: {needle} is gone — {why}")
    if 'KieeKeyProbeSolvedRect' not in main or 'KieeKeyProbeSolvedRect' not in probe:
        failures.append(
            "main.cpp / tools/ui_probe/ui_probe.cpp: KieeKeyProbeSolvedRect() is gone "
            "(BS-22g) — every rectangle finding has to name the rectangle the app's "
            "own solver applied: \"needs 744px in a 629px box\" is only actionable "
            "when it says whether that box came from the solver or from an older "
            "layout")
    if 'g_settingsScroll.solved' not in main:
        failures.append("main.cpp: the solved baseline is gone (BS-22g) — the scroll "
                        "machinery repositions page children from it")
    if 'bool needBar = (a.contentBottom[tab] > s.page.bottom);' not in probe:
        failures.append(
            "tools/ui_probe/ui_probe.cpp: the bar ruler is no longer the CURRENT "
            "tab's depth (BS-22) — the scroll range is per tab "
            "(settingsScrollSetTab sets nMax/nPage from perTabContentBottom[tab]), "
            "so judging Win32's usable/dead answer over all nine tabs reports a dead "
            "bar as a defect on every state whose own tab fits while a deeper tab "
            "lives in the same dialog")
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
    print("PAINT RULES OK — 11 rules: sibling clipping, window styles, "
          "repaint-after-change, need-deduped reflow, unscrolled solver input, "
          "rescale-owns-its-resolve + total baseline drop (BS-18), symmetric "
          "scrollbar correction (+ the probe's blank-page, reflow, pixel and "
          "operation-sequence checks), authored solver input (BS-22), measure-"
          "after-clamp + the probe's derived expectations (BS-22), one owner for the bar pair + narrow-only clamp + live-rectangle regions (BS-22c)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
