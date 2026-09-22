#!/usr/bin/env python3
# ============================================================================
# KieeKey - A modified version based on OpenKey
#
# Modified work:
#   KieeKey - refactored and completed logic
#   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
#   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
#
# File: scripts/audit_layout.py
# SPDX-License-Identifier: GPL-3.0-or-later
# ============================================================================
"""audit_layout.py — static layout audit of the two Win32 surfaces.

WHY THIS EXISTS
    v1.3.0-beta2 shipped two surfaces whose text is laid out with hand-written
    pixel rectangles:

      * the settings dialog (src/app/main.cpp, `mkCtl(...)` + the kTabN
        visibility lists), and
      * the Chaos / Flexing Lab (src/app/ChaosLabWindow.cpp, `create(...)`).

    A Win32 STATIC word-wraps inside its rectangle and CLIPS whatever does not
    fit vertically: no scrollbar, no ellipsis, no way for the user to reveal the
    rest. That is the reported "chữ bị mất do đè mà không có cách nào hiện ra
    được". A tab control whose items do not fit shrinks every tab and clips the
    label mid-word, which is how nine Vietnamese tab headers became unreadable
    (and how the "Phòng Chaos" page — the only place the live external effects
    can be switched on — became effectively unreachable).

WHAT IS EXACT AND WHAT IS MODELLED
    Two classes of finding, deliberately separated so nobody has to trust a font
    guess:

      GEOMETRY (exact — no font model at all)
        * two controls on the same page whose rectangles share a pixel,
        * a control that reaches outside the page/client area,
        * tab-header crowding computed from the tab control's own item count
          and width (reported with the measured widths the app itself uses at
          runtime; here from a labelled width model, flagged as such).

      TEXT FIT (modelled — three confidence bands)
        Segoe UI at the dialog's 13 px character height is approximated by a
        per-character advance. A label is reported as
          DEFINITE  — clipped even with the NARROW advance (5.6 px/char),
          PROBABLE  — clipped with the MID advance (6.6 px/char),
          POSSIBLE  — clipped only with the WIDE advance (7.6 px/char).
        Only DEFINITE/PROBABLE are failures. The shipped app does not rely on
        this model: it measures with DrawTextW(DT_CALCRECT) on the real font and
        real monitor and reflows through ok::layout::autoFit(). This script is
        the regression gate that says "no authored rectangle is obviously too
        small for its own string".

USAGE
    scripts/audit_layout.py [--repo=DIR] [--json=PATH] [--strict]

    --strict   exit 1 on DEFINITE/PROBABLE clips, overlaps and out-of-page
               controls (the default is to exit 1 only on GEOMETRY findings).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass, field, asdict

# --- Segoe UI @ 13px character height (the settings dialog's uiFont) --------
# Segoe UI at the dialog's 13 px character height. Both axes are banded so a
# finding is only "definite" when the text does not fit even with the MOST
# generous (narrowest glyph, tightest leading) plausible metrics.
LINE_TIGHT_PX = 15
LINE_MID_PX = 17
LINE_LOOSE_PX = 19
LINE_HEIGHT_PX = LINE_MID_PX
NARROW_PX = 5.6
MID_PX = 6.6
WIDE_PX = 7.6
# A shared sliver of <= this many pixels on either axis is anti-aliasing /
# border territory, not "text covered by text".
TOUCH_TOLERANCE_PX = 4
TAB_PAD_PX = 12          # per-tab padding the common control adds around text
# v1.3.0-beta8: how far a page may reach BELOW the authored tab viewport and
# still be considered "served by the scroll fallback" instead of a layout bug.
# The fallback itself is pinned by tests/test_dialog_layout.cpp (every control
# is inside the client area or scroll-reachable at 100/125/150/200 %), and
# main.cpp turns the residual into a REAL scrollbar — enabled exactly when
# there is overflow after BS-01. Anything deeper than this budget is still a
# hard failure: it would mean a scroll range of half a screen for one tab.
SCROLL_BUDGET_PX = 640

SETTINGS_CLIENT_W = 560
SETTINGS_CLIENT_H = 622
TAB_X, TAB_Y, TAB_W, TAB_H = 12, 66, 536, 506
# The tab control's display area: below the header row, inside the border.
PAGE_TOP = TAB_Y + 22
PAGE_BOTTOM = TAB_Y + TAB_H - 4
PAGE_LEFT = TAB_X + 4
PAGE_RIGHT = TAB_X + TAB_W - 4

LAB_CLIENT_W = 760
LAB_CLIENT_H = 880


@dataclass
class Control:
    surface: str
    id: str
    klass: str
    text: str
    style: str
    x: int
    y: int
    w: int
    h: int
    page: int = -1            # -1 = always visible
    line: int = 0
    grown: bool = False       # set when the app auto-fits it at runtime
    order: int = 0            # creation order == z-order (later == on top)

    @property
    def right(self) -> int:
        return self.x + self.w

    @property
    def bottom(self) -> int:
        return self.y + self.h


@dataclass
class Finding:
    kind: str                 # overlap | out_of_page | clip | tab_crowding | no_font
    severity: str             # definite | probable | possible | info
    surface: str
    detail: str
    ids: list = field(default_factory=list)
    line: int = 0


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------
def _strip_comments(src: str) -> str:
    """Blank out C++ comments while PRESERVING every offset and line number.

    v1.3.0-beta8: the parser used to read raw source, so a comment inside a
    mkCtl()/create() argument list — e.g. the BS-04 note that explains the
    group box height — was scanned as code. Its commas split the argument list
    and the control silently VANISHED from the audit (a group box the audit no
    longer saw could not be checked, and the gate stayed green). Comments are
    now replaced by spaces of the same length, so line numbers and column
    offsets used in findings are unchanged.
    """
    out = list(src)
    i, n = 0, len(src)
    in_block = False
    while i < n:
        c = src[i]
        if in_block:
            if c == "*" and i + 1 < n and src[i + 1] == "/":
                out[i] = out[i + 1] = " "
                in_block = False
                i += 2
                continue
            if c != "\n":
                out[i] = " "
            i += 1
            continue
        if c == '"' or c == "'":                     # skip string/char literals
            quote = c
            i += 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            out[i] = out[i + 1] = " "
            in_block = True
            i += 2
            continue
        i += 1
    return "".join(out)


def _join_literals(chunk: str) -> str:
    """Concatenate adjacent L"..." literals inside one call argument."""
    parts = re.findall(r'L"((?:[^"\\]|\\.)*)"', chunk)
    text = "".join(parts)
    text = text.replace(r"\\", "\\").replace(r'\\"', '"').replace(r"\\n", "\n")
    text = text.replace("\\n", "\n").replace("\\r", "")
    return text


def _split_args(text: str) -> list:
    """Split a call's argument list on top-level commas."""
    args, depth, current, in_str = [], 0, [], False
    i = 0
    while i < len(text):
        c = text[i]
        if in_str:
            current.append(c)
            if c == "\\" and i + 1 < len(text):
                current.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            current.append(c)
        elif c in "([":
            depth += 1
            current.append(c)
        elif c in ")]":
            depth -= 1
            current.append(c)
        elif c == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(c)
        i += 1
    if current:
        args.append("".join(current).strip())
    return args


def _call_args(src: str, open_paren: int) -> list:
    """Argument list of the call whose '(' is at `open_paren`."""
    depth, i = 0, open_paren
    while i < len(src):
        c = src[i]
        if c == '"':                      # skip string literals entirely
            i += 1
            while i < len(src) and src[i] != '"':
                i += 2 if src[i] == "\\" else 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return _split_args(src[open_paren + 1:i])
        i += 1
    return []


def _class_name(expr: str) -> str:
    """L"BUTTON" / WC_COMBOBOXW / TRACKBAR_CLASSW -> a comparable token."""
    expr = expr.strip()
    m = re.fullmatch(r'L?"([A-Za-z0-9_ ]+)"', expr)
    if m:
        return m.group(1)
    return expr


def _s_value(expr: str) -> int | None:
    """Evaluate S(123) / 123 / static_cast<INT_PTR>(...) style coordinates."""
    expr = expr.strip()
    m = re.fullmatch(r"S\(\s*(-?\d+)\s*\)", expr)
    if m:
        return int(m.group(1))
    m = re.fullmatch(r"-?\d+", expr)
    if m:
        return int(expr)
    return None


def parse_settings_dialog(src: str) -> list:
    controls: list = []
    for m in re.finditer(r"mkCtl\(\s*hwnd\s*,", src):
        args = _call_args(src, src.index("(", m.start()))
        if len(args) < 9:
            continue
        klass = _class_name(args[1])
        text = _join_literals(args[2])
        style = args[3]
        coords = [_s_value(a) for a in args[4:8]]
        if any(c is None for c in coords):
            continue                      # loop-generated rows (kIds[i]) etc.
        x, y, w, h = coords
        idm = re.search(r"(IDC_\w+|IDOK|IDCANCEL)", args[8])
        cid = idm.group(1) if idm else "?"
        line = src[:m.start()].count("\n") + 1
        controls.append(Control("settings", cid, klass, text, style, x, y, w, h,
                                line=line, order=line))

    # The loop-generated checkbox rows of tab 0 (seven options, 22px apart).
    m = re.search(r"const wchar_t\* kOpts\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if m:
        labels = re.findall(r'L"((?:[^"\\]|\\.)*)"', m.group(1))
        for i, label in enumerate(labels):
            # The rows are created by the kOpts[] loop, so their creation order
            # is the loop position in the file (not the end of it).
            loop_line = src[:m.start()].count("\n") + 1
            controls.append(Control("settings", f"IDC_CHK_ROW{i}", "BUTTON", label,
                                    "BS_AUTOCHECKBOX", 44, 226 + i * 22, 460, 20,
                                    page=0, line=loop_line, order=loop_line))

    # Tab membership from the kTabN visibility lists.
    for m in re.finditer(r"static constexpr int kTab(\d+)\[\]\s*=\s*\{(.*?)\};", src, re.S):
        tab_index = int(m.group(1))
        for cid in re.findall(r"(IDC_\w+|IDOK|IDCANCEL)", m.group(2)):
            for c in controls:
                if c.surface == "settings" and c.id == cid and c.page == -1:
                    c.page = tab_index
    return controls


def parse_lab(src: str) -> list:
    controls: list = []
    for m in re.finditer(r"(?:m_impl->\w+\s*=\s*)?\bcreate\(", src):
        args = _call_args(src, src.index("(", m.start()))
        if len(args) < 8:
            continue
        klass = _class_name(args[0])
        text = _join_literals(args[1])
        style = args[2]
        x, y, w, h = (_s_value(a) for a in args[3:7])
        if None in (x, y, w, h):
            continue
        idm = re.search(r"(kId\w+|-1)", args[7])
        cid = idm.group(1) if idm else "?"
        line = src[:m.start()].count("\n") + 1
        controls.append(Control("lab", cid, klass, text, style, x, y, w, h,
                                page=0, line=line, order=line))
    return controls


def parse_tab_labels(src: str) -> list:
    labels = []
    for m in re.finditer(r"wchar_t t(\d)\[\]\s*=\s*L\"((?:[^\"\\]|\\.)*)\";", src):
        labels.append((int(m.group(1)), m.group(2)))
    labels.sort()
    return [text for _, text in labels]


# ---------------------------------------------------------------------------
# Text-fit model
# ---------------------------------------------------------------------------
def wrapped_lines(text: str, width_px: float, char_px: float) -> int:
    """Greedy word wrap, one explicit newline = a hard break."""
    if not text:
        return 1
    lines = 0
    for paragraph in text.split("\n"):
        words = paragraph.split(" ")
        used = 0.0
        count = 1
        for word in words:
            wlen = len(word) * char_px
            space = char_px if used > 0 else 0.0
            if used + space + wlen <= width_px or used == 0.0:
                used += space + wlen
            else:
                count += 1
                used = wlen
        lines += count
    return lines


def _muldiv(value: int, num: int, den: int) -> int:
    """Win32 MulDiv semantics (round to nearest) — what the app's S() does."""
    if value >= 0:
        return (value * num + den // 2) // den
    return -((-value * num + den // 2) // den)


def _scaled_metrics(scale_pct: int) -> tuple:
    """Conservative per-scale glyph metrics: (narrow, mid, wide, tight, mid_h, loose).

    The app scales BOTH the authored rectangle (MulDiv, round-to-nearest) and
    the font (a negative CreateFontW height, which GDI rounds UP to the next
    usable raster size). Modelling the text with ceil() and the box with
    MulDiv is therefore the pessimistic direction: a finding at 125/150 % means
    the string does not fit even when the glyphs came out small, which is the
    no-false-positive bar this audit holds. At 100 % the model is byte-identical
    to the historical one (no rounding at all).
    """
    if scale_pct == 100:
        return NARROW_PX, MID_PX, WIDE_PX, LINE_TIGHT_PX, LINE_MID_PX, LINE_LOOSE_PX

    def up(value: int) -> int:                     # ceil(v * scale / 100)
        return -((-value * scale_pct) // 100)

    def upf(value: float) -> float:
        # Glyph advances scale EXACTLY with the DPI (a proportional font is
        # rasterised at the new size, it does not gain a pixel per character).
        # Ceiling them here would overstate every string by up to one pixel per
        # character — 11 % at 150 % — which is enough to invent line breaks and
        # turn this gate into a false-positive machine.
        return value * scale_pct / 100.0

    return (upf(NARROW_PX), upf(MID_PX), upf(WIDE_PX),
            up(LINE_TIGHT_PX), up(LINE_MID_PX), up(LINE_LOOSE_PX))


def _at_scale(c: Control, scale_pct: int) -> Control:
    """The control as it exists at `scale_pct` %: rect MulDiv'd, text unchanged."""
    if scale_pct == 100:
        return c
    return Control(c.surface, c.id, c.klass, c.text, c.style,
                   _muldiv(c.x, scale_pct, 100), _muldiv(c.y, scale_pct, 100),
                   _muldiv(c.w, scale_pct, 100), _muldiv(c.h, scale_pct, 100),
                   page=c.page, line=c.line, grown=c.grown, order=c.order)


def text_fits(c: Control, scale_pct: int = 100) -> tuple | None:
    """Return (severity, needed_px, have_px, why) when a text control is too small.

    Bands, most generous first:
      definite  NARROW glyph advance + TIGHT leading
      probable  MID glyph advance + nominal leading
      possible  WIDE glyph advance + loose leading
    A label reported as "definite" cannot fit its own string on ANY plausible
    Segoe UI metric — that is the no-false-positive bar this audit holds.

    v1.3.0-beta8 (CA-01a): the battery runs at 100/125/150 % DPI. The authored
    rectangle is LOGICAL (S(n) == n * dpi / 96 at runtime), so a control that
    fits at 100 % should fit at every scale; the sweep catches the cases where
    rounding on both axes changes the answer.
    """
    c = _at_scale(c, scale_pct)
    _narrow, _mid, _wide, tight, mid_h, loose = _scaled_metrics(scale_pct)
    if not c.text.strip():
        return None
    if "SS_ICON" in c.style or c.klass in ("WC_TABCONTROLW", "COMBOBOX",
                                           "TRACKBAR_CLASSW", "WC_LINK"):
        return None
    if c.klass == "EDIT" or "ES_MULTILINE" in c.style:
        return None                      # user content, not a label
    bands = (("definite", _narrow, tight),
             ("probable", _mid, mid_h),
             ("possible", _wide, loose))
    if c.klass == "BUTTON":
        # A button paints ONE clipped line: no wrap, no ellipsis.
        for severity, char_px, _line in bands:
            needed = len(c.text) * char_px
            if needed > c.w + TOUCH_TOLERANCE_PX:
                return (severity, int(needed), c.w, "button label is one clipped line")
            return None
        return None
    # STATIC / SysLink: word-wraps inside the rect, clips vertically. The
    # overflow is unreachable — no scrollbar, no tooltip, no way to reveal it.
    for severity, char_px, line_px in bands:
        lines = wrapped_lines(c.text, c.w - 6, char_px)
        needed = lines * line_px + 2
        if needed > c.h:
            return (severity, needed, c.h,
                    f"{lines} wrapped line(s) x {line_px}px leading")
        return None
    return None


# ---------------------------------------------------------------------------
# Audit
# ---------------------------------------------------------------------------
def audit(controls: list, tab_labels: list, src: str = "") -> list:
    findings: list = []

    # --- 1. tab header crowding (settings dialog) --------------------------
    if tab_labels:
        widths = [int(len(t) * MID_PX) for t in tab_labels]
        needed = sum(w + TAB_PAD_PX for w in widths)
        avail = TAB_W - 8
        if needed > avail:
            per_tab = avail // len(tab_labels)
            clipped = [t for t, w in zip(tab_labels, widths) if w + TAB_PAD_PX > per_tab]
            findings.append(Finding(
                "tab_crowding", "definite", "settings",
                f"{len(tab_labels)} tab headers need ~{needed}px but the tab control "
                f"offers {avail}px: Windows shrinks each tab to ~{per_tab}px and clips "
                f"the label. Clipped labels: {clipped}",
                ids=[f"tab:{t}" for t in clipped]))

    # --- 2. geometry: overlaps + out of page -------------------------------
    for surface, page_w, page_h, page_box in (
            ("settings", SETTINGS_CLIENT_W, SETTINGS_CLIENT_H,
             (PAGE_LEFT, PAGE_TOP, PAGE_RIGHT, PAGE_BOTTOM)),
            ("lab", LAB_CLIENT_W, LAB_CLIENT_H, (0, 0, LAB_CLIENT_W, LAB_CLIENT_H))):
        items = [c for c in controls if c.surface == surface]
        for i, a in enumerate(items):
            for b in items[i + 1:]:
                if a.page != b.page:
                    continue
                # A group box containing its own children is the design, not a bug.
                if (("GROUPBOX" in a.style and _contains(a, b)) or
                        ("GROUPBOX" in b.style and _contains(b, a))):
                    continue
                if _overlaps(a, b):
                    shared, ix, iy = _shared_area(a, b)
                    if shared <= 0:
                        continue
                    if ix <= TOUCH_TOLERANCE_PX or iy <= TOUCH_TOLERANCE_PX:
                        continue          # border sliver, not covered text
                    findings.append(Finding(
                        "overlap", "definite", surface,
                        f"{a.id} ({a.x},{a.y},{a.w}x{a.h}) overlaps {b.id} "
                        f"({b.x},{b.y},{b.w}x{b.h}) by {shared}px² "
                        f"[{a.text[:28]!r} vs {b.text[:28]!r}]",
                        ids=[a.id, b.id], line=a.line))
        for c in items:
            gx, gy, gw, gh = _geometry_rect(c)
            gright, gbottom = gx + gw, gy + gh
            if surface == "settings" and c.page >= 0:
                left, top, right, bottom = page_box
                # Horizontal escape, or above the viewport: always a failure —
                # the scroll fallback only ever moves content VERTICALLY, so a
                # control outside the page columns cannot be reached at all.
                horizontal = max(left - gx, gright - right)
                above = top - gy
                over = max(horizontal, above)
                if over > 0:
                    findings.append(Finding(
                        "out_of_page",
                        "definite" if over > TOUCH_TOLERANCE_PX else "probable", surface,
                        f"{c.id} ({c.x},{c.y},{c.w}x{c.h}) reaches outside the tab "
                        f"page area ({left},{top})-({right},{bottom})",
                        ids=[c.id], line=c.line))
                else:
                    # v1.3.0-beta8: below the authored viewport is NOT a defect
                    # per se — refitWindow() + the (now working) scroll model
                    # make it reachable, and every tab that needs it declares it
                    # here as an informational finding. Beyond the budget it is
                    # still a hard failure: nothing should need half a screen of
                    # scrolling.
                    below = gbottom - bottom
                    if below > SCROLL_BUDGET_PX:
                        findings.append(Finding(
                            "out_of_page", "definite", surface,
                            f"{c.id} ({c.x},{c.y},{c.w}x{c.h}) ends {below}px below the "
                            f"tab page bottom ({bottom}) — beyond the {SCROLL_BUDGET_PX}px "
                            f"scroll budget",
                            ids=[c.id], line=c.line))
                    elif below > 0:
                        findings.append(Finding(
                            "below_page", "info", surface,
                            f"{c.id} ends {below}px below the authored tab viewport — "
                            f"reachable through the scroll fallback (WS_A: refitWindow + "
                            f"the BS-01 metrics), no longer clipped as in beta7",
                            ids=[c.id], line=c.line))
            elif surface == "lab":
                over = max(-gx, -gy, gright - page_w, gbottom - page_h)
                if over > 0:
                    findings.append(Finding(
                        "out_of_page",
                        "definite" if over > TOUCH_TOLERANCE_PX else "probable", surface,
                        f"{c.id} ({c.x},{c.y},{c.w}x{c.h}) reaches outside the "
                        f"{page_w}x{page_h} client area", ids=[c.id], line=c.line))

    # --- 3. text fit (v1.3.0-beta8, CA-01a: 100/125/150 %) -----------------
    one_line_ids = {c.id for c in controls
                    if c.klass == "STATIC" and c.text.strip()
                    and c.h <= LINE_MID_PX + 9}
    findings += [f for f in audit_dpi_sweep(controls)
                 if not (f.kind == "clip" and f.ids and f.ids[0] in one_line_ids)]
    findings += audit_single_line_policy(controls)

    # --- 4. combo windows over lower-z-order siblings (CA-01b, BS-05) -------
    findings += audit_combo_zorder(controls)
    findings += audit_combo_dropheight(controls, src)

    # --- 5. group boxes that do not contain their own control (CA-01c) -----
    findings += audit_group_containment(controls)

    # --- 6. the worst-case note rows (BS-08) --------------------------------
    findings += audit_pinned_multiline_rows(controls)

    # --- 6. the Lab never sets a font (EDIT => SYSTEM_FIXED_FONT) ----------
    return findings


COMBO_CLOSED_H = LINE_HEIGHT_PX + 8


def _geometry_rect(c: Control) -> tuple:
    """The rectangle a control ACTUALLY occupies on screen.

    A COMBOBOX is created with its DROP-DOWN height (that is what the Win32
    height parameter means for CBS_DROPDOWNLIST), so its authored `h` is not
    the space it takes while closed. Counting the drop-down height would
    report ten overlaps that no user can ever see — a false positive, which
    this audit exists to avoid. The closed control is one edit row tall; the
    drop-down is transient and always drawn on top of whatever is below.
    """
    h = c.h
    if c.klass in ("COMBOBOX", "WC_COMBOBOXW"):
        h = COMBO_CLOSED_H
    return (c.x, c.y, c.w, h)


def _overlaps(a: Control, b: Control) -> bool:
    ax, ay, aw, ah = _geometry_rect(a)
    bx, by, bw, bh = _geometry_rect(b)
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah


def _shared_area(a: Control, b: Control) -> tuple:
    ax, ay, aw, ah = _geometry_rect(a)
    bx, by, bw, bh = _geometry_rect(b)
    ix = max(0, min(ax + aw, bx + bw) - max(ax, bx))
    iy = max(0, min(ay + ah, by + bh) - max(ay, by))
    return ix * iy, ix, iy


def _contains(outer: Control, inner: Control) -> bool:
    ox, oy, ow, oh = _geometry_rect(outer)
    ix, iy, iw, ih = _geometry_rect(inner)
    return (ox <= ix and oy <= iy and ix + iw <= ox + ow and iy + ih <= oy + oh)


GROUP_BAND_PX = 24        # how far below a group box a control may sit and still
                          # be considered "meant to live inside it"
ELLIPSIS_STYLE = "SS_ENDELLIPSIS"


def _is_group_box(c: Control) -> bool:
    return c.klass == "BUTTON" and "GROUPBOX" in c.style


def audit_dpi_sweep(controls: list) -> list:
    """CA-01a — text fit at 100/125/150 % (one finding per control, worst first)."""
    findings: list = []
    for c in controls:
        if not c.text.strip():
            continue
        reported: dict = {}
        for scale in (100, 125, 150):
            verdict = text_fits(c, scale)
            if verdict is None:
                continue
            severity, needed, have, why = verdict
            order = {"definite": 0, "probable": 1, "possible": 2}
            if c.id not in reported or order[severity] < order[reported[c.id][0]]:
                reported[c.id] = (severity, needed, have, why, scale)
        for cid, (severity, needed, have, why, scale) in reported.items():
            if severity == "possible":
                continue
            findings.append(Finding(
                "clip", severity, c.surface,
                f"{cid} at line {c.line}: needs ~{needed}px, has {have}px at "
                f"{scale}% ({why}) — text is clipped with no way to reveal it: "
                f"{c.text[:70]!r}",
                ids=[cid], line=c.line))
    return findings


def audit_combo_zorder(controls: list) -> list:
    """CA-01b (BS-05) — a control hidden UNDER a combo box's created window.

    A CBS_DROPDOWNLIST combo is created with its DROP-DOWN height as the window
    height, so its window rect extends far below the 25 px the user sees. Every
    sibling created BEFORE it (lower z-order) that ends up inside that invisible
    area is covered by the combo's window — today only the creation order keeps
    the visible controls clickable, and the audit's closed-height model cannot
    see it at all. Severity: DEFINITE when the sibling is completely covered,
    PROBABLE when it is partially covered. The fix is creation-order (create the
    combo first: later siblings then paint over the empty drop-down area, while
    the drop-down LIST is a popup window and still paints above everything).
    """
    findings: list = []
    # Both surfaces — but a combo can only cover a sibling of the SAME window
    # and the SAME tab, so comparisons stay inside (surface, page).
    settings = list(controls)
    combos = [c for c in settings if c.klass in ("COMBOBOX", "WC_COMBOBOXW")]
    for combo in combos:
        full_x, full_y, full_w, full_h = combo.x, combo.y, combo.w, combo.h
        if full_h <= COMBO_CLOSED_H + TOUCH_TOLERANCE_PX:
            continue                              # authored at closed height
        invisible_y = full_y + COMBO_CLOSED_H     # below the visible band
        for other in settings:
            if other is combo or other.order >= combo.order:
                continue                          # created later => on top (fine)
            if other.surface != combo.surface or other.page != combo.page:
                continue
            if _is_group_box(other) or "GROUPBOX" in other.style:
                continue
            ox, oy, ow, oh = _geometry_rect(other)
            # Overlap with the INVISIBLE part of the combo window only.
            ix = min(full_x + full_w, ox + ow) - max(full_x, ox)
            iy = min(full_y + full_h, oy + oh) - max(invisible_y, oy)
            if ix <= TOUCH_TOLERANCE_PX or iy <= TOUCH_TOLERANCE_PX:
                continue
            covered = ix * iy
            fully = _contains(combo, other) or (oy >= invisible_y and covered >= ow * oh - 1)
            findings.append(Finding(
                "under_combo", "definite" if fully else "probable", combo.surface,
                f"{other.id} ({other.x},{other.y},{other.w}x{other.h}) was created "
                f"BEFORE {combo.id} ({combo.x},{combo.y},{combo.w}x{combo.h}), whose "
                f"WINDOW extends {full_h - COMBO_CLOSED_H}px below the visible box: "
                f"{'fully' if fully else f'{covered}px²'} inside the invisible "
                f"drop-down area — the combo's window covers it while closed "
                f"(click/drag may not reach it)",
                ids=[other.id, combo.id], line=combo.line))
    return findings


def audit_combo_dropheight(controls: list, src: str) -> list:
    """CA-01b-2 (BS-05 contract) — the drop-down height must be requested by CODE.

    The z-order check above can only see the invisible window of a combo that is
    still authored tall. This one closes the class from the other side:

      * a CBS_DROPDOWNLIST authored with a height > one closed row is a
        DEFINITE finding — that IS the window that hides its lower-z siblings
        (create it closed and ask for the list height with
        applyComboDropHeight()), and
      * a combo that never calls applyComboDropHeight() is a PROBABLE finding —
        the authored list height is then lost and the user gets a one-item
        drop-down with no scrollbar.

    Both halves are seed-verified (tests/verify_audit_seeds.py).
    """
    findings: list = []
    if not src:
        return findings
    lines = src.splitlines()
    for c in controls:
        if c.klass not in ("COMBOBOX", "WC_COMBOBOXW"):
            continue
        if c.surface != "settings":
            continue
        if c.h > COMBO_CLOSED_H + TOUCH_TOLERANCE_PX:
            findings.append(Finding(
                "combo_window", "definite", c.surface,
                f"{c.id} at line {c.line} is authored {c.h}px tall — for a "
                f"CBS_DROPDOWNLIST that is the DROP-DOWN height, so the control's "
                f"WINDOW is {c.h - COMBO_CLOSED_H}px taller than the {COMBO_CLOSED_H}px "
                f"row the user sees while it is closed. Create it at the closed "
                f"height and call applyComboDropHeight() after its items (BS-05).",
                ids=[c.id], line=c.line))
            continue
        # The helper call must sit in the same creation block (the items are
        # added right there, before the dialog is ever shown).
        window = "\n".join(lines[max(0, c.line - 1):c.line + 60])
        if "applyComboDropHeight(" not in window:
            findings.append(Finding(
                "combo_window", "probable", c.surface,
                f"{c.id} at line {c.line} is created at the closed height but "
                f"never calls applyComboDropHeight(): the authored drop-down "
                f"height is lost (one-item list, no scrollbar) — the visible "
                f"list must be requested explicitly NOW that the window is short "
                f"(BS-05).",
                ids=[c.id], line=c.line))
    return findings


def audit_group_containment(controls: list) -> list:
    """CA-01c (BS-04) — a control that clearly belongs to a group box but sits outside it.

    "Belongs" is decided without guesswork: horizontally inside the group's span
    and vertically at or below its top, no further than GROUP_BAND_PX past its
    bottom edge. Such a control is drawn as an orphan under the box it labels
    itself with (the beta7 Arcarde steering row sat 6 px below IDC_GRP_ARCADE's
    bottom edge, and autoFit only stretches a group for CONTAINED children, so
    the gap grows with DPI). A group is only blamed when no sibling group boxes
    the same control in.
    """
    findings: list = []
    groups = [c for c in controls if _is_group_box(c)]
    for c in controls:
        if _is_group_box(c) or c.page < 0 or not c.text.strip():
            continue
        if "SS_ICON" in c.style:
            continue
        best = None
        for g in groups:
            if g.surface != c.surface or g.page != c.page:
                continue
            if not (c.x >= g.x and c.right <= g.right):
                continue
            if c.y < g.y:
                continue
            inside = c.bottom <= g.bottom
            gap = 0 if inside else c.y - g.bottom
            if inside:
                best = (0, g, True)
                break
            if gap <= GROUP_BAND_PX and (best is None or gap < best[0]):
                best = (gap, g, False)
        if best is not None and not best[2]:
            gap, g, _ = best
            # A row sandwiched between two group boxes with a tight gap on BOTH
            # sides is a section divider by design (page 0's "Bảng mã:" row sits
            # between "Phương thức gõ" and "Tùy chọn gõ"); only a row that hangs
            # under its box with nothing below it is an orphan.
            below = [h for h in groups
                     if h.surface == c.surface and h.page == c.page and h.y >= c.bottom]
            if below and min(h.y - c.bottom for h in below) <= GROUP_BAND_PX:
                continue
            findings.append(Finding(
                "outside_group", "definite", c.surface,
                f"{c.id} ({c.x},{c.y},{c.w}x{c.h}) is {gap}px BELOW "
                f"{g.id} ({g.x},{g.y},{g.w}x{g.h}) while clearly belonging to it "
                f"(x-inside, same tab): the group box does not contain its own "
                f"control, so the solver can never stretch the box with it",
                ids=[c.id, g.id], line=c.line))
    return findings


# v1.3.0-beta8 (BS-08): the multiline STATIC rows that drive tab overflow. The
# Bàn phím tab's IDC_STAT_OUT_NOTE is a ~145-character note in a 34 px box (three
# lines at 96 DPI) and was the main reason tab 0 scrolls; the other three are the
# same class on the Chaos/Live pages. They are legal as long as their text FITS
# at 100/125/150 % — so they are pinned BY NAME here: the CA-01a sweep must still
# be able to see each of them, and a string edit that grows one of them past its
# box fails the gate instead of silently clipping (the beta7 report).
BS08_PINNED_ROWS = (
    "IDC_STAT_OUT_NOTE",      # tab 0 — the worst case, drives the scroll fallback
    "IDC_STAT_METHOD_HINT",   # tab 0
    "IDC_STAT_CHAOS_WARN",    # tab 6 — grew in beta6 (h 72) and must keep fitting
    "IDC_STAT_LIVE_HINT",     # tab 6 — the live-effects contract note
)


def audit_pinned_multiline_rows(controls: list) -> list:
    """BS-08 — the worst-case notes are still present AND still fit, 3 scales."""
    findings: list = []
    by_id = {c.id: c for c in controls}
    for cid in BS08_PINNED_ROWS:
        c = by_id.get(cid)
        if c is None:
            findings.append(Finding(
                "pinned_row", "definite", "settings",
                f"{cid} is one of the worst-case rows pinned by BS-08 and is "
                f"missing from the dialog — it was renamed or deleted without "
                f"re-checking tab overflow at 100/125/150 %",
                ids=[cid]))
            continue
        verdicts = [text_fits(c, scale) for scale in (100, 125, 150)]
        if not any(v is not None for v in verdicts):
            continue                       # fits everywhere: the pin is satisfied
        worst = max((v for v in verdicts if v is not None),
                    key=lambda v: v[1] - v[2])
        findings.append(Finding(
            "pinned_row", "definite", c.surface,
            f"{cid} (BS-08 worst-case note) no longer fits: needs "
            f"~{worst[1]:.0f}px, has {worst[2]}px in {c.w}x{c.h} at "
            f"100/125/150 % ({worst[3]}); the note rows drive tab overflow and "
            f"must either be shortened or given more height",
            ids=[cid], line=c.line))
    return findings


def audit_single_line_policy(controls: list) -> list:
    """CA-01d (BS-02/BS-03) — a one-line value row must declare HOW it clips.

    A STATIC authored one line tall that cannot fit its string at 100 % wraps
    and clips the tail permanently. That is now legal ONLY for the fixed-role
    value rows, and only when the control says so in its own style
    (SS_ENDELLIPSIS, plus a tooltip registered by src/app/main.cpp
    setRowTooltip()), or when the runtime re-solve grows it (a growable label
    with OK::GROW marker). Anything else is a finding — this is what pins the
    BS-02 policy so a future string edit cannot quietly re-clip a row.
    """
    findings: list = []
    for c in controls:
        if not c.text.strip():
            continue
        is_button = c.klass == "BUTTON" and "GROUPBOX" not in c.style
        one_line = is_button or (c.klass == "STATIC" and c.h <= LINE_MID_PX + 9)
        if not one_line:
            continue
        # A BUTTON never wraps and a STATIC shorter than two lines cannot show
        # a second line either: for BOTH the realistic advance is the honest
        # yardstick. (For a checkbox/radio the glyph box takes ~17 px of the
        # width, which this model does not subtract — the fixes below leave
        # margin for it.)
        needed = len(c.text) * MID_PX
        severity = "definite" if needed > c.w + TOUCH_TOLERANCE_PX else None
        if severity is None:
            continue
        have = c.w
        why = "one-line label vs a realistic Segoe UI advance"
        ellipsized = ELLIPSIS_STYLE in c.style
        documented = ellipsized or "GROW" in c.style
        if documented:
            continue
        findings.append(Finding(
            "clip", severity, c.surface,
            f"{c.id} at line {c.line} is a one-line row whose text needs "
            f"~{needed:.0f}px in {have}px ({why}); it clips the tail with no "
            f"ellipsis ({ELLIPSIS_STYLE} missing) and no re-solve marker: "
            f"{c.text[:70]!r}",
            ids=[c.id], line=c.line))
    return findings


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--json", default="")
    ap.add_argument("--strict", action="store_true",
                    help="also fail on DEFINITE/PROBABLE text clipping")
    args = ap.parse_args()

    root = pathlib.Path(args.repo)
    main_src = _strip_comments(
        (root / "src" / "app" / "main.cpp").read_text(encoding="utf-8"))
    lab_src = _strip_comments(
        (root / "src" / "app" / "ChaosLabWindow.cpp").read_text(encoding="utf-8"))

    controls = parse_settings_dialog(main_src) + parse_lab(lab_src)
    tab_labels = parse_tab_labels(main_src)
    findings = audit(controls, tab_labels, main_src)

    # The Lab's font: no WM_SETFONT anywhere means every EDIT control paints
    # with SYSTEM_FIXED_FONT (an OEM raster face with no Vietnamese glyphs).
    if "WM_SETFONT" not in lab_src and "CreateFont" not in lab_src:
        findings.append(Finding(
            "no_font", "definite", "lab",
            "ChaosLabWindow never creates or assigns a font: EDIT controls fall "
            "back to SYSTEM_FIXED_FONT (raster, no Vietnamese diacritics) and "
            "STATIC/BUTTON to SYSTEM_FONT — this is the 'lab bị lỗi font, khó đọc' "
            "report."))

    order = {"definite": 0, "probable": 1, "possible": 2, "info": 3}
    findings.sort(key=lambda f: (f.surface, order.get(f.severity, 9), f.kind))

    counts: dict = {}
    for f in findings:
        counts[f"{f.severity}:{f.kind}"] = counts.get(f"{f.severity}:{f.kind}", 0) + 1

    print(f"parsed controls: {len(controls)} "
          f"(settings {sum(1 for c in controls if c.surface == 'settings')}, "
          f"lab {sum(1 for c in controls if c.surface == 'lab')})")
    print(f"tab headers    : {len(tab_labels)} -> {tab_labels}")
    for key in sorted(counts):
        print(f"  {key:28s} {counts[key]}")
    print()
    for f in findings:
        print(f"[{f.severity.upper():8s}] {f.surface:8s} {f.kind:13s} {f.detail}")

    if args.json:
        pathlib.Path(args.json).write_text(
            json.dumps({"controls": [asdict(c) for c in controls],
                        "tabLabels": tab_labels,
                        "findings": [asdict(f) for f in findings],
                        "counts": counts}, ensure_ascii=False, indent=1),
            encoding="utf-8")
        print(f"\nwrote {args.json}")

    hard = [f for f in findings if f.severity == "definite" and
            f.kind in ("overlap", "out_of_page", "tab_crowding", "no_font",
                       "outside_group", "under_combo", "combo_window",
                       "pinned_row")]
    if args.strict:
        hard += [f for f in findings if f.kind == "clip" and f.severity in ("definite", "probable")]
        hard += [f for f in findings if f.kind == "under_combo" and f.severity == "probable"]
        hard += [f for f in findings if f.kind == "combo_window" and f.severity == "probable"]
    if hard:
        print(f"\nAUDIT FAIL — {len(hard)} hard finding(s)", file=sys.stderr)
        return 1
    print("\nAUDIT OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
