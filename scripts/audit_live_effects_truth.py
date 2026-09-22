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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# File: scripts/audit_live_effects_truth.py
# SPDX-License-Identifier: GPL-3.0-or-later
# ============================================================================
"""v1.3.0-beta8 (FT-03 / FT-05) — the Live-Effects tab must tell the truth.

The tester's report was never "the effects do not work" — it was that the tab
did not SAY what it was doing:

  * the gate readout could be clipped or stale (which condition is blocking the
    effects: IME off, app excluded, box unticked, non-Unicode table?);
  * F9 and Ctrl+Alt+F12 silently fought the Arcade Hub for the same keys;
  * the "+1,8 ns/ký tự" cost note in the dialog could drift away from the
    number docs/PERFORMANCE.md actually measured.

main.cpp is Windows-only, so these invariants are checked as SOURCE contracts:

  1. ONE gate model: liveGateStatusText() is built from liveGateNow(), and the
     diagnostics report uses the same gate (no second, divergent answer).
  2. The gate row is written BOTH at dialog creation and in the runtime refresh
     path — and it goes through markOneLineRow(), the BS-02 fit path, so the
     longest blocker sentence cannot be silently cut off.
  3. F9 (tone style) is gated on ArcadeManager::isConsumingKeyboard(): while a
     game owns the keyboard the key belongs to the game (FT-05 / risk R1).
  4. Ctrl+Alt+F12 is still the emergency off switch and still matches only when
     the effects were actually on.
  5. The documented cost ("+1,8 ns/ký tự") in the dialog and in
     docs/PERFORMANCE.md carry the SAME figure.

Pure source inspection — no Windows needed. Exit 0 = all layers pass.
"""

from __future__ import annotations

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(REPO, "src", "app", "main.cpp")
PERF = os.path.join(REPO, "docs", "PERFORMANCE.md")


def fail(msg: str) -> None:
    print("  FAIL:", msg)


def strip_comments(src: str) -> str:
    """Blank // comments offset-preservingly (line numbers stay valid)."""
    out = []
    for line in src.splitlines(keepends=True):
        idx = line.find("//")
        if idx >= 0:
            line = line[:idx] + " " * (len(line) - idx)
        out.append(line)
    return "".join(out)


def main() -> int:
    raw = open(MAIN, encoding="utf-8").read()
    src = strip_comments(raw)
    ok = True

    # ---- 1. one gate model ------------------------------------------------
    if "GateBlocker liveGateNow(" not in src:
        fail("liveGateNow() is gone — the tab could disagree with the evidence")
        ok = False
    else:
        print("  ok  liveGateNow() is the single gate model")

    if "liveGateStatusText()" not in src:
        fail("liveGateStatusText() is gone — the tab has no readable gate line")
        ok = False
    else:
        print("  ok  liveGateStatusText() renders the gate")

    gate_sites = src.count("liveGateStatusText()")
    if gate_sites < 4:
        fail(f"liveGateStatusText() appears only {gate_sites}x — it must be used "
             f"for the control text, the runtime refresh AND the diagnostics "
             f"report (report + create + refresh = 3+ call sites)")
        ok = False
    else:
        print(f"  ok  {gate_sites} call sites (create + refresh + report)")

    parts = raw.split("buildDiagReportUtf8")
    if len(parts) < 2:
        fail("buildDiagReportUtf8() is gone — the Chẩn đoán export has no report "
             "builder, so it cannot carry the live-gate line (DS-01/DS-05)")
        ok = False
    elif "liveGateStatusText" not in parts[1][:400]:
        fail("the diagnostics report no longer carries the live-gate line — the "
             "export and the tab would tell different stories (DS-05)")
        ok = False
    else:
        print("  ok  the diagnostics report carries the same gate line")

    # ---- 2. the gate row is refreshed, through the one-line fit path ------
    refresh = re.search(
        r"markOneLineRow\(::GetDlgItem\(hwnd, IDC_STAT_LIVE_GATE\),",
        src)
    if refresh is None:
        fail("IDC_STAT_LIVE_GATE is not refreshed through markOneLineRow() — a "
             "longer blocker sentence would be silently clipped (BS-02/FT-03)")
        ok = False
    else:
        print("  ok  the gate row is refreshed through markOneLineRow()")

    if src.count("IDC_STAT_LIVE_GATE") < 3:
        fail("IDC_STAT_LIVE_GATE is referenced fewer than 3 times "
             "(create + hide lists + refresh)")
        ok = False
    else:
        print("  ok  the gate row is created, listed and refreshed")

    # ---- 3. F9 belongs to the game while a game owns the keyboard ---------
    # The producer thread may not call ArcadeManager (check_input_isolation.py),
    # so the answer travels through the AppState mirror: the UI timer publishes
    # it, the hook reads the atomic.
    f9_pos = src.find("ev.vkCode == VK_F9")
    if f9_pos < 0:
        fail("the F9 tone-style handler is gone")
        ok = False
    else:
        head = src[:f9_pos]
        guard = head.rfind("g.arcadeOwnsKeyboard.load(")
        if guard < 0 or f9_pos - guard > 900:
            fail("F9 is handled without the arcadeOwnsKeyboard guard — the "
                 "hotkey would fight the Arcade Hub during a run (FT-05 / R1)")
            ok = False
        else:
            print("  ok  F9 is guarded by the arcadeOwnsKeyboard mirror (FT-05 / R1)")

    if not re.search(r"g\.arcadeOwnsKeyboard\.store\(\s*\n?\s*"
                     r"ok::arcade::ArcadeManager::instance\(\)\.isConsumingKeyboard\(\)",
                     src):
        fail("nothing publishes the arcadeOwnsKeyboard mirror from "
             "isConsumingKeyboard() — the guard would read a stale false forever")
        ok = False
    else:
        print("  ok  the UI tick publishes the mirror from isConsumingKeyboard()")

    producer = re.search(r"PD onHookEventImpl\(const KeyEvent& ev\)(.*?)\n\}", src, re.S)
    if producer is not None and "ArcadeManager" in producer.group(1):
        fail("onHookEventImpl calls ArcadeManager directly — "
             "scripts/check_input_isolation.py forbids game singletons in the "
             "global IME producer")
        ok = False

    # ---- 4. Ctrl+Alt+F12 stays the emergency switch ----------------------
    if not re.search(r"ev\.vkCode == VK_F12 && ev\.modifiers\.ctrl[^;]*ev\.modifiers\.alt",
                     src, re.S):
        fail("the Ctrl+Alt+F12 emergency switch is gone — only the checkbox and "
             "the tray menu could turn the effects off")
        ok = False
    else:
        print("  ok  Ctrl+Alt+F12 emergency disable present")

    if "g.liveEffects.disable()" not in src:
        fail("the emergency switch no longer disables the effects")
        ok = False

    # ---- 5. the documented cost matches the measured figure ---------------
    def figure(text: str) -> str | None:
        m = re.search(r"\+\s*([0-9]+[.,][0-9])\s*ns", text)
        return m.group(1).replace(".", ",") if m else None

    dialog = figure(src)
    doc = figure(open(PERF, encoding="utf-8").read()) if os.path.isfile(PERF) else None
    if dialog is None:
        fail("the Live-Effects hint no longer states the measured cost "
             "(docs/PERFORMANCE.md is the authority)")
        ok = False
    elif doc is None:
        print("  note: docs/PERFORMANCE.md has no '+X,Y ns' figure to compare")
    elif dialog != doc:
        fail(f"the dialog claims +{dialog} ns/char but docs/PERFORMANCE.md "
             f"documents +{doc} ns/char — one of them is stale")
        ok = False
    else:
        print(f"  ok  the cost note (+{dialog} ns/ký tự) matches docs/PERFORMANCE.md")

    if not ok:
        print("AUDIT FAIL — the live-effects tab would not tell the truth")
        return 1
    print("AUDIT OK — live-effects gate, hotkeys and cost note all agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
