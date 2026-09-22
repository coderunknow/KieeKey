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
        'mkCtl(hwnd, L"STATIC", L"Nhịp Rhythm (60-220):",\n'
        '                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(280), S(368), S(160), S(20)',
        'mkCtl(hwnd, L"STATIC", L"Nhịp Rhythm (60-220):",\n'
        '                  WS_CHILD | WS_VISIBLE | SS_LEFT, S(280), S(368), S(100), S(20)',
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
        'const bool isStatic = (clsLen == 6 && ::lstrcmpiW(cls, L"STATIC") == 0);',
        'const bool isStatic = (clsLen == 6 && wcscmp(cls, L"STATIC") == 0);',
        "class_case",
    ),

]


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

    if failures:
        print(f"\nSEED VERIFICATION FAILED — {failures} case(s)")
        return 1
    print("\nALL SEEDED VIOLATIONS CAUGHT (the audit is not vacuously green)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
