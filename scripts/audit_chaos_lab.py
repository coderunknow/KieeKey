#!/usr/bin/env python3
#============================================================================
# KieeKey - A modified version based on OpenKey
#
# Original work:
#   OpenKey - Vietnamese input method engine
#   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
#   Licensed under the GNU General Public License version 3.
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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# File: scripts/audit_chaos_lab.py
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
"""v1.3.0-beta6 (V3) — SOURCE-CONTRACT audit for the Chaos Lab window.

audit_settings_wiring.py pins the settings DIALOG's 37 controls; the Chaos
Lab (src/app/ChaosLabWindow.cpp) is a second interactive surface driving the
same engine singletons and never got the treatment. Same three layers:

  1. WIRING     every kId* control is CREATED by open() and its state is
                consumed somewhere in labWndProc — either its own
                `control == kId…` branch, a WM_HSCROLL comparison (the
                trackbar), or a BM_GETCHECK read inside another handler
                (the two "modifier" checkboxes: inject / per-chunk).
  2. EFFECT     each handler branch reaches an engine consumer:
                applyCheckboxes / refreshPreview (ChaosEngine config or
                preview redraw), ensureFlexingGame / applyFlexGranularity /
                pumpFlexing / typeIntoFocusApp / manager.handleKey (the
                flexing pipeline).
  3. PERSIST    every ChaosEngine knob the Lab can change survives a
                restart: the registry key appears in BOTH getDword and
                setDword form in src/app/main.cpp (loadSettings /
                saveSettings). The Lab itself never writes the registry —
                saveSettings() mirrors the singleton on every change point
                and on the clean-exit sweep, which is the contract checked
                here.
  4. REPORT (v1.3.0-beta8, FT-02)  the Flexing "gõ thật ra app" path must never
                fail silently: typeIntoFocusApp() returns an ok::flexsend::Outcome
                and every call site reports it (status row + one-time dialog on a
                refused activation). Checked as a source contract: the portable
                policy header must be included and its reason table consulted at
                least at the three reporting sites (flex send, send-once, the
                injection timer), and no bare `(void)typeIntoFocusApp` may remain.
  5. DPI (v1.3.0-beta8, BS-06)  the Lab has no layout solver: its design is a
                fixed 96-DPI table, so EVERY child must be in the rescale
                table (impl.children) AND the DPI branch must both re-lay-out
                the children and re-fit the window. beta7 rescaled only the
                font — at 125/150 % the glyphs grew inside unchanged boxes and
                every label was clipped ("chữ bị đè" in the Lab). Checked
                here because it is a source contract, and pinned by
                audit_layout.py's 100/125/150 % text-fit sweep.

Pure source inspection — no Windows needed. Exit 0 = all layers pass.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.path.join(REPO, "src", "app", "ChaosLabWindow.cpp")
MAIN = os.path.join(REPO, "src", "app", "main.cpp")

# Control ids whose consumption happens inside ANOTHER handler's branch:
# the id is read there via the control's HWND (BM_GETCHECK), not by its own
# `control ==` comparison. Value = the consuming handler + read verb.
MODIFIER_CONTROLS = {
    "kIdInject":     "read by kIdSendOnce via BM_GETCHECK(impl->injectBox)",
    "kIdFlexInject": "read by kIdFlexSend via BM_GETCHECK(impl->flexInject)",
}

# Output/data controls: they have no OWN event branch by design. Each entry
# documents who reads or writes them, so a dead control can never hide
# behind this table without the documentation going stale too.
PASSIVE_CONTROLS = {
    "kIdPreview":   "output: refreshPreview writes it (SetWindowTextW); "
                    "read by kIdSendOnce via controlText(impl->preview)",
    "kIdFlexOutput": "output: appendToEdit / SetWindowTextW (engine product)",
    "kIdFlexPrep":  "data: read by ensureFlexingGame via "
                    "controlText(impl->flexPrep) -> setPreloadedText",
    # v1.3.0-beta8 (FT-02): the "why nothing was typed" row. It has no event
    # branch by design — reportSendOutcome() writes it from the three send
    # sites (flex send, send-once, the injection timer). A silent send is now
    # impossible to miss, which is exactly what beta7 got wrong.
    "kIdStatus":    "output: written by reportSendOutcome() (ok::flexsend "
                    "reasonVi/statusLineVi) from the send sites",
}

# Tokens that prove a handler branch reaches an engine consumer.
CONSUMERS = ("applyCheckboxes", "refreshPreview", "ensureFlexingGame",
             "applyFlexGranularity", "pumpFlexing", "typeIntoFocusApp",
             "manager.handleKey")


def fail(msg: str) -> None:
    print("  FAIL:", msg)


def main() -> int:
    lab = open(LAB, encoding="utf-8").read()
    main = open(MAIN, encoding="utf-8").read()
    ok = True

    ids = sorted(set(re.findall(r"constexpr (?:int|UINT_PTR) (kId\w+)\s*=", lab)))
    print(f"chaos lab controls: {len(ids)}")

    for cid in ids:
        # ---- layer 1a: created by open() ----------------------------------
        created = re.search(r"create\([^;]*?\b" + cid + r"\b", lab, re.S) is not None
        if not created:
            fail(f"{cid}: never created (no create(...) call)")
            ok = False
            continue

        # ---- layer 1b: consumed in labWndProc -----------------------------
        if cid in PASSIVE_CONTROLS:
            print(f"  ok  {cid:16s} created, passive [{PASSIVE_CONTROLS[cid]}]")
            continue
        if cid in MODIFIER_CONTROLS:
            consumed = f"modifier ({MODIFIER_CONTROLS[cid]})"
        else:
            own_branch = f"control == {cid}" in lab
            hscroll = (cid == "kIdIntensity" and
                       "lParam) == impl->intensity" in lab)
            en_change = re.search(
                r"control == " + cid + r" && notification == EN_CHANGE", lab)
            if not (own_branch or hscroll):
                fail(f"{cid}: created but no handler branch / HSCROLL route")
                ok = False
                continue
            consumed = "own branch" + (" + WM_HSCROLL" if hscroll else "")
            if en_change:
                consumed += " (EN_CHANGE)"

        # ---- layer 2: effect — the branch body reaches a consumer ---------
        if cid not in MODIFIER_CONTROLS:
            m = re.search(r"control == " + cid + r"\b(.*?)(?:if \(control == |break;)",
                          lab, re.S)
            window = m.group(1) if m else lab
            if cid == "kIdIntensity":
                # The trackbar also applies through the WM_HSCROLL arm,
                # which shares the applyCheckboxes() path.
                window += lab
            if not any(tok in window for tok in CONSUMERS):
                fail(f"{cid}: handler does not reach an engine consumer")
                ok = False
                continue
        print(f"  ok  {cid:16s} created, consumed [{consumed}]")

    # ---- layer 3: persistence of every Lab-writable ChaosEngine knob ------
    # registry key -> what it carries. Both getDword and setDword must exist
    # in main.cpp (loadSettings / saveSettings mirror the singleton).
    KEYS = {
        "ChaosMaster":               "master switch",
        "ChaosCase":                 "random-case switch",
        "ChaosGlyph":                "glyph-transform switch",
        "ChaosIntensityPercent":     "case intensity (Lab slider)",
        "ChaosGlyphIntensityPercent": "glyph intensity",
        "ChaosGlyphMode":            "glyph mode (Lab combo)",
        "ChaosCaseGranularity":      "case granularity",
    }
    for key, what in KEYS.items():
        got = main.count(f'getDword(L"{key}"')
        put = main.count(f'setDword(L"{key}"')
        if got < 1 or put < 1:
            fail(f"registry key {key} ({what}): get={got} set={put} "
                 f"— every Lab-writable knob needs both")
            ok = False
        else:
            print(f"  ok  {key:28s} get+set present ({what})")

    # ---- layer 4: the DPI contract (BS-06) -------------------------------
    # Every created child must be recorded in the rescale table: the probe is
    # the create() wrapper, which both remembers the authored 96-DPI rect and
    # creates the control. A child created by a raw CreateWindowExW would be
    # silently left unscaled.
    # Inside create() the raw Win32 call is expected (that IS the wrapper); what
    # must not happen is a SECOND raw creation site elsewhere in the file — such
    # a child would never be in the rescale table. Exactly one is legal outside
    # the wrapper: the top-level window itself.
    wrapper = re.search(r"const auto create = \[&\]\(.*?\n        \};", lab, re.S)
    outside = lab if wrapper is None else lab[:wrapper.start()] + lab[wrapper.end():]
    stray = len(re.findall(r"::CreateWindowExW\(", outside))
    if wrapper is None or stray != 1:
        fail(f"{stray} raw CreateWindowExW call(s) outside the create() wrapper "
             f"(expected exactly the top-level window) — such a child would never "
             f"be rescaled on DPI change")
        ok = False
    else:
        print("  ok  every lab control is created through create() (rescale table)")

    if "children.push_back" not in lab:
        fail("create() does not record the authored rect in impl.children — "
             "applyLabLayout() would have nothing to rescale")
        ok = False
    else:
        print("  ok  create() records every child in impl.children")

    for token, why in (("applyLabFont", "the font"),
                       ("applyLabLayout", "the child rects"),
                       ("fitLabWindowToDesign", "the window that holds them")):
        if token not in lab:
            fail(f"WM_DPICHANGED does not call {token}() — {why} would not "
                 f"follow the DPI (BS-06)")
            ok = False
        else:
            print(f"  ok  DPI change re-applies {why} ({token})")

    # All three must sit in the WM_DPICHANGED arm, not merely exist somewhere.
    m = re.search(r"case WM_DPICHANGED:(.*?)(?:\n\s{8}case |\Z)", lab, re.S)
    if m is None:
        fail("ChaosLabWindow.cpp has no WM_DPICHANGED arm")
        ok = False
    else:
        arm = m.group(1)
        missing = [tok for tok in ("applyLabFont", "applyLabLayout",
                                   "fitLabWindowToDesign") if tok not in arm]
        if missing:
            fail(f"WM_DPICHANGED does not re-apply: {', '.join(missing)} — "
                 f"the Lab would keep 96-DPI rectangles at 125/150 % (BS-06)")
            ok = False
        else:
            print("  ok  WM_DPICHANGED re-applies font + child rects + window fit")

    # ---- layer 4: the silent-failure contract (FT-02) ---------------------
    if '#include "FlexSendOutcome.hpp"' not in lab:
        fail("ChaosLabWindow.cpp does not include FlexSendOutcome.hpp — the "
             "Flexing send path has no reason table (FT-02)")
        ok = False
    else:
        print("  ok  Flexing send path reports through ok::flexsend")

    # The reason table must be consulted where the user is told (the status row
    # and the dialog), and the shared reporter must be called from at least the
    # three send sites: flex send, send-once and the injection timer.
    if "statusLineVi(" not in lab or "reasonVi(" not in lab or "detailVi(" not in lab:
        fail("the reason table is not consulted where the user is told — the "
             "status row and the dialog must both come from ok::flexsend")
        ok = False
    else:
        print("  ok  the status row and the dialog both use the reason table")
    reports = lab.count("reportSendOutcome(")
    if reports < 4:   # declaration + definition + >= 2 call sites... see below
        fail(f"only {reports} reportSendOutcome occurrence(s) — expected the "
             f"declaration, the definition and at least two call sites")
        ok = False
    else:
        print(f"  ok  {reports} reportSendOutcome occurrence(s) "
              f"(declaration + definition + call sites)")

    if "(void)typeIntoFocusApp" in lab:
        fail("a call site still discards the send outcome — the user would see "
             "nothing when the injection cannot start (FT-02)")
        ok = False
    else:
        print("  ok  every typeIntoFocusApp() result is reported")

    if "SetWindowTextW(impl.status" not in lab and "SetWindowTextW(impl->status" not in lab:
        fail("the send status row is never written — the reason would go nowhere")
        ok = False
    else:
        print("  ok  the send status row is written by the lab")

    if not ok:
        print("AUDIT FAIL — chaos lab control contract violated")
        return 1
    print("AUDIT OK — every chaos lab control is created, wired, "
          "engine-connected, DPI-rescaled, self-reporting and persisted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
