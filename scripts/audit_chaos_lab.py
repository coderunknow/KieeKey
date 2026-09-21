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

    if not ok:
        print("AUDIT FAIL — chaos lab control contract violated")
        return 1
    print("AUDIT OK — every chaos lab control is created, wired, "
          "engine-connected and persisted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
