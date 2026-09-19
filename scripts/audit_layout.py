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
        controls.append(Control("settings", cid, klass, text, style, x, y, w, h, line=line))

    # The loop-generated checkbox rows of tab 0 (seven options, 22px apart).
    m = re.search(r"const wchar_t\* kOpts\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if m:
        labels = re.findall(r'L"((?:[^"\\]|\\.)*)"', m.group(1))
        for i, label in enumerate(labels):
            controls.append(Control("settings", f"IDC_CHK_ROW{i}", "BUTTON", label,
                                    "BS_AUTOCHECKBOX", 44, 218 + i * 22, 460, 20,
                                    page=0))

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
        controls.append(Control("lab", cid, klass, text, style, x, y, w, h, page=0, line=line))
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


def text_fits(c: Control) -> tuple | None:
    """Return (severity, needed_px, have_px, why) when a text control is too small.

    Bands, most generous first:
      definite  NARROW glyph advance + TIGHT leading
      probable  MID glyph advance + nominal leading
      possible  WIDE glyph advance + loose leading
    A label reported as "definite" cannot fit its own string on ANY plausible
    Segoe UI metric — that is the no-false-positive bar this audit holds.
    """
    if not c.text.strip():
        return None
    if "SS_ICON" in c.style or c.klass in ("WC_TABCONTROLW", "COMBOBOX",
                                           "TRACKBAR_CLASSW", "WC_LINK"):
        return None
    if c.klass == "EDIT" or "ES_MULTILINE" in c.style:
        return None                      # user content, not a label
    bands = (("definite", NARROW_PX, LINE_TIGHT_PX),
             ("probable", MID_PX, LINE_MID_PX),
             ("possible", WIDE_PX, LINE_LOOSE_PX))
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
def audit(controls: list, tab_labels: list) -> list:
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
                over = max(left - gx, gright - right, top - gy, gbottom - bottom)
                if over > 0:
                    findings.append(Finding(
                        "out_of_page",
                        "definite" if over > TOUCH_TOLERANCE_PX else "probable", surface,
                        f"{c.id} ({c.x},{c.y},{c.w}x{c.h}) reaches outside the tab "
                        f"page area ({left},{top})-({right},{bottom})",
                        ids=[c.id], line=c.line))
            elif surface == "lab":
                over = max(-gx, -gy, gright - page_w, gbottom - page_h)
                if over > 0:
                    findings.append(Finding(
                        "out_of_page",
                        "definite" if over > TOUCH_TOLERANCE_PX else "probable", surface,
                        f"{c.id} ({c.x},{c.y},{c.w}x{c.h}) reaches outside the "
                        f"{page_w}x{page_h} client area", ids=[c.id], line=c.line))

    # --- 3. text fit -------------------------------------------------------
    for c in controls:
        if not c.text.strip():
            continue
        verdict = text_fits(c)
        if verdict is None:
            continue
        severity, needed, have, why = verdict
        findings.append(Finding(
            "clip", severity, c.surface,
            f"{c.id} at line {c.line}: needs ~{needed}px, has {have}px ({why}) — "
            f"text is clipped with no way to reveal it: {c.text[:70]!r}",
            ids=[c.id], line=c.line))

    # --- 4. the Lab never sets a font (EDIT => SYSTEM_FIXED_FONT) ----------
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--json", default="")
    ap.add_argument("--strict", action="store_true",
                    help="also fail on DEFINITE/PROBABLE text clipping")
    args = ap.parse_args()

    root = pathlib.Path(args.repo)
    main_src = (root / "src" / "app" / "main.cpp").read_text(encoding="utf-8")
    lab_src = (root / "src" / "app" / "ChaosLabWindow.cpp").read_text(encoding="utf-8")

    controls = parse_settings_dialog(main_src) + parse_lab(lab_src)
    tab_labels = parse_tab_labels(main_src)
    findings = audit(controls, tab_labels)

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
            f.kind in ("overlap", "out_of_page", "tab_crowding", "no_font")]
    if args.strict:
        hard += [f for f in findings if f.kind == "clip" and f.severity in ("definite", "probable")]
    if hard:
        print(f"\nAUDIT FAIL — {len(hard)} hard finding(s)", file=sys.stderr)
        return 1
    print("\nAUDIT OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
