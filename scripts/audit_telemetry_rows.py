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
# File: scripts/audit_telemetry_rows.py
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
"""v1.3.0-beta5 (bug B3) — SOURCE-CONTRACT audit for the diagnostics rows.

THE BUG
    The tab-3 row "Sự kiện bàn phím đã xử lý" was fed by
    `g.hook.pushed()` — the SPSC ring counter that keyboard events AND
    mouse button/wheel events AND foreground changes ALL increment. The
    number therefore climbed while the user merely dragged the mouse
    (the exact tester report). HookCounters (v1.3.0-beta3) already kept
    the sources apart; the UI display binding was never switched over.

WHY A GREP AUDIT AND NOT A UNIT TEST
    The binding lives in src/app/main.cpp — a Windows-only translation
    unit that the portable Linux suite cannot compile. The semantics of
    the counters themselves ARE unit-tested (tests/test_hook_counters.cpp:
    keyboardEvents() excludes mouse/foreground, etc.). What this script
    pins is the missing third layer: WHICH counter each UI row displays.
    It fails the build if any row's swprintf-to-SetWindowTextW binding
    drifts back to a wrong source, if the per-source rows disappear, or
    if Win32Wrapper loses the counters() forwarder the rows read through.

Exit 0 = every row is bound to its contract source. Exit 1 = drift.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# row id -> (tokens that MUST appear in the row's value expression,
#            tokens that must NOT appear there)
ROW_CONTRACT = {
    # The keyboard row may ONLY ever show keyboardEvents() — never the
    # ring counter (pushed) or the all-sources total.
    "IDC_STAT_PUSHV":  (["keyboardEvents()"], ["pushed()", "allSources()"]),
    "IDC_STAT_MOUSEV": (["mouseEvents()"],    ["pushed()", "keyboardEvents()"]),
    "IDC_STAT_FGV":    (["foregroundChanged"], ["pushed()", "keyboardEvents()"]),
    # The ring counter keeps its OWN row, labelled "mọi nguồn".
    "IDC_STAT_RINGV":  (["pushed()"],         ["keyboardEvents()"]),
    "IDC_STAT_DROPV":  (["dropped()"],        ["pushed()"]),
}


def fail(msg: str) -> None:
    print(f"TELEMETRY AUDIT FAILED: {msg}")
    sys.exit(1)


def binding_region(lines, row_idx):
    """Walk back from a SetWindowTextW(row) line to its swprintf; return the
    joined source text of the value expression (swprintf .. write, incl.)."""
    for j in range(row_idx, max(-1, row_idx - 16), -1):
        if "std::swprintf" in lines[j]:
            return "\n".join(lines[j:row_idx + 1])
    return None


def main() -> int:
    main_cpp = (REPO / "src/app/main.cpp").read_text(encoding="utf-8")
    wrapper = (REPO / "src/core/win32_wrapper.hpp").read_text(encoding="utf-8")

    # 1. Win32Wrapper must forward the per-source counters (the rows read
    #    g.hook.counters(); without the forwarder main.cpp cannot compile,
    #    but the audit states the contract explicitly anyway).
    if not re.search(r"const\s+ok::hook::HookCounters&\s+counters\(\)", wrapper):
        fail("Win32Wrapper lost the counters() forwarder (bug B3 contract)")

    lines = main_cpp.splitlines()

    # 2. Every contracted row exists in WM_CREATE (mkCtl) and is written in
    #    the timer block from its contract source.
    for row, (must, must_not) in ROW_CONTRACT.items():
        writes = [i for i, ln in enumerate(lines)
                  if row in ln and "SetWindowTextW" in ln]
        if not writes:
            fail(f"{row} is never written by the telemetry timer")
        region = binding_region(lines, writes[0])
        if region is None:
            fail(f"{row}: cannot find the swprintf feeding its SetWindowTextW")
        for token in must:
            if token not in region:
                fail(f"{row}: value expression must contain {token!r} "
                     f"(found: {region.strip()[:160]!r})")
        for token in must_not:
            if token in region:
                fail(f"{row}: value expression must NOT contain {token!r} "
                     f"({token} is a different source — the beta4 bug)")

    # 3. The rows must be created (mkCtl) and be part of tab 3's visibility
    #    list, or the values would be written to dead ids.
    for cid in ("IDC_STAT_MOUSELAB", "IDC_STAT_MOUSEV", "IDC_STAT_FGLAB",
                "IDC_STAT_FGV", "IDC_STAT_RINGLAB", "IDC_STAT_RINGV"):
        if not re.search(rf"HMENU>\({cid}\)", main_cpp):
            fail(f"{cid} is not created in WM_CREATE")
    tab3 = re.search(r"static constexpr int kTab3\[\] = \{(.*?)\};",
                     main_cpp, re.S)
    if not tab3:
        fail("kTab3 visibility list not found")
    for cid in ("IDC_STAT_MOUSELAB", "IDC_STAT_MOUSEV", "IDC_STAT_FGLAB",
                "IDC_STAT_FGV", "IDC_STAT_RINGLAB", "IDC_STAT_RINGV"):
        if cid not in tab3.group(1):
            fail(f"{cid} missing from kTab3 (row would never be shown)")

    # 4. The keyboard row's LABEL may not claim the ring counter's meaning:
    #    "đã xử lý" (processed — all sources) next to a keyboard-only number
    #    is the wording that made beta2/beta4 misleading.
    lab = re.search(r'L"([^"]*)",\s*WS_CHILD[^;]*IDC_STAT_PUSHLAB\)', main_cpp, re.S)
    if not lab or "đã xử lý" in lab.group(1):
        fail("IDC_STAT_PUSHLAB label must not say 'đã xử lý' for a "
             "keyboard-only counter (bug B3 wording contract)")

    print("TELEMETRY AUDIT OK — every diagnostics row is bound to its "
          "contract source (keyboard/mouse/foreground/ring kept apart)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
