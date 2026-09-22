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
# File: scripts/audit_feature_persistence.py
# SPDX-License-Identifier: GPL-3.0-or-later
# ============================================================================
"""FT-01 wiring grep-gate (v1.3.0-beta8).

WHY THIS EXISTS
    In beta7 the progression/AI engines were fully implemented, fully tested,
    and never called: `grep -rn "saveToFile" src/app/` returned nothing, so XP
    and the learned AI profile were discarded on exit. A unit test cannot see
    that — the units were fine. This audit is the missing link: it reads
    src/app/main.cpp (which needs windows.h to compile) and asserts the CALL
    SITES exist on every path the durability claim depends on:

      boot      — the load runs right after loadMacros()
      exit      — WM_ENDSESSION (logoff) and WM_DESTROY (clean quit)
      tick      — the 30 s crash-safe sweep is wired into the 1 s status tick
      opt-in    — the AI file is written only while the user is opted in
      policy    — file names + the throttle come from PersistPolicy.hpp

    It is deliberately source-textual: it fails on a code MOVE that removes a
    call site, which is exactly the regression class that produced FT-01.

Usage:  python3 scripts/audit_feature_persistence.py [--repo=<root>]
Exit:   0 = every call site present, 1 = at least one missing.
"""
from __future__ import annotations

import os
import re
import sys

HARD: list[str] = []


def _fail(msg: str) -> None:
    HARD.append(msg)


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def _strip_comments(src: str) -> str:
    """Blank C++ comments, preserving offsets/newlines (see audit_layout.py)."""
    out = list(src)
    i, n = 0, len(src)
    in_line = in_block = in_str = in_chr = False
    while i < n:
        ch = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if in_line:
            if ch == "\n":
                in_line = False
            else:
                out[i] = " "
        elif in_block:
            if ch == "*" and nxt == "/":
                out[i] = out[i + 1] = " "
                i += 1
                in_block = False
            elif ch != "\n":
                out[i] = " "
        elif in_str or in_chr:
            if ch == "\\":
                out[i] = " "
                if i + 1 < n and src[i + 1] != "\n":
                    out[i + 1] = " "
                i += 1
            elif (in_str and ch == '"') or (in_chr and ch == "'"):
                in_str = in_chr = False
        elif ch == "/" and nxt == "/":
            out[i] = out[i + 1] = " "
            i += 1
            in_line = True
        elif ch == "/" and nxt == "*":
            out[i] = out[i + 1] = " "
            i += 1
            in_block = True
        elif ch == '"':
            in_str = True
        elif ch == "'":
            in_chr = True
        i += 1
    return "".join(out)


def _line_of(src: str, needle: str) -> int:
    idx = src.find(needle)
    return 0 if idx < 0 else src[:idx].count("\n") + 1


def main() -> int:
    root = "."
    for arg in sys.argv[1:]:
        if arg.startswith("--repo="):
            root = arg.split("=", 1)[1]
    main_cpp = os.path.join(root, "src", "app", "main.cpp")
    policy = os.path.join(root, "src", "app", "PersistPolicy.hpp")
    if not os.path.isfile(main_cpp):
        print(f"audit_feature_persistence: missing {main_cpp}")
        return 1
    raw = _read(main_cpp)
    src = _strip_comments(raw)

    # 1. The engines' contracts are actually called.
    if "saveToFile(" not in src:
        _fail("main.cpp never calls ProgressionEngine::saveToFile() — XP is lost on exit (FT-01)")
    if "loadFromFile(" not in src:
        _fail("main.cpp never calls ProgressionEngine::loadFromFile() — every session starts at level 1 (FT-01)")
    if "serializeProfile(" not in src:
        _fail("main.cpp never calls AiRivalEngine::serializeProfile() — the learned profile is lost (FT-01)")
    if "deserializeProfile(" not in src:
        _fail("main.cpp never calls AiRivalEngine::deserializeProfile() — the learned profile is never restored (FT-01)")

    # 2. The load runs on the BOOT path, right after the macros are loaded.
    boot = re.search(r"loadMacros\(\s*\)\s*;(.{0,900}?)loadProgressionAtBoot\(\s*\)\s*;",
                     src, re.S)
    if not boot:
        _fail("loadProgressionAtBoot() is not called right after loadMacros() on the boot path")
    else:
        print(f"  boot load      : main.cpp:{_line_of(src, 'loadProgressionAtBoot();')} (after loadMacros())")

    # 3. Both exit paths sweep.
    end_session = re.search(r"case\s+WM_ENDSESSION:(.{0,2000})", src, re.S)
    if not end_session or "saveProgressionAndAi(" not in end_session.group(1):
        _fail("WM_ENDSESSION does not call saveProgressionAndAi() — a logoff silently drops XP (FT-01)")
    else:
        print(f"  WM_ENDSESSION  : main.cpp:{_line_of(src, 'case WM_ENDSESSION:')} teardown sweep present")
    # There are two WM_DESTROY handlers (the settings dialog and the hidden
    # main window) — the main window's teardown is the one that must sweep.
    destroy_ok = None
    for m in re.finditer(r"case\s+WM_DESTROY:(.{0,2000})", src, re.S):
        if "saveProgressionAndAi(" in m.group(1):
            destroy_ok = m
            break
    if destroy_ok is None:
        _fail("WM_DESTROY does not call saveProgressionAndAi() — a clean quit silently drops XP (FT-01)")
    else:
        line = src[:destroy_ok.start()].count("\n") + 1
        print(f"  WM_DESTROY     : main.cpp:{line} final sweep present")

    # 4. The crash-safe sweep rides the 1 s status tick.
    # The tick handler is extracted with a BRACE BALANCE, not a fixed character
    # window: a comment added anywhere in the function used to be able to push
    # the first "}" past the window and fail this check for no reason. The call
    # must still be the FIRST thing the tick does (first 400 characters of the
    # real body), so the crash-safety contract is not weakened.
    notch = src.find("void onNotifyTick()")
    body = None
    if notch >= 0:
        open_brace = src.find("{", notch)
        if open_brace >= 0:
            depth = 0
            for i in range(open_brace, len(src)):
                if src[i] == "{":
                    depth += 1
                elif src[i] == "}":
                    depth -= 1
                    if depth == 0:
                        body = src[open_brace + 1:i]
                        break
    if body is None or "maybeSaveProgressionThrottled(" not in body[:400]:
        _fail("onNotifyTick() does not call maybeSaveProgressionThrottled() — a crash loses the whole session")

    # 5. The AI profile is written only while opted in.
    save_ai = re.search(r"bool\s+saveProgressionAndAi\(\s*\)\s*\{(.*?)\n\}", src, re.S)
    if not save_ai:
        _fail("saveProgressionAndAi() not found — the exit sweep has no implementation")
    else:
        body = save_ai.group(1)
        if "shouldPersistAi(" not in body or "isOptIn(" not in body:
            _fail("the AI profile write is not gated on the opt-in flag")

    # 6. Paths / throttle come from the portable policy header, and the paths
    #    are handed to the std::string_view APIs as UTF-8 (not filesystem::path,
    #    which would not compile and would mangle a non-ASCII %APPDATA%).
    if not os.path.isfile(policy):
        _fail("src/app/PersistPolicy.hpp is missing (FT-01 rules live there)")
    else:
        pol = _strip_comments(_read(policy))
        for needle in ('kProgressionFileName', 'kAiProfileFileName',
                       'shouldPersistAi', 'kPersistThrottleMs'):
            if needle not in pol:
                _fail(f"PersistPolicy.hpp lost {needle}")
        for needle in ("kProgressionFileName", "kAiProfileFileName",
                       "shouldPersistAi", "persistSweepDue"):
            if needle not in src:
                _fail(f"main.cpp does not use ok::apppolicy::{needle}")
    if "utf16ToUtf8(progPath)" not in src:
        _fail("the progression path is not converted to UTF-8 before the string_view API")
    if "filesystem::path" in src:
        _fail("main.cpp passes std::filesystem::path to a string_view API (does not compile)")

    # 7. The progress FILE is not the registry and not a .json lie.
    if os.path.isfile(policy):
        pol_text = _read(policy)
        if "progression.dat" not in pol_text or "aiprofile.dat" not in pol_text:
            _fail("the persistence file names changed away from progression.dat/aiprofile.dat")

    if HARD:
        print("\nPERSISTENCE AUDIT FAIL")
        for m in HARD:
            print(f"  [FAIL] {m}")
        print(f"  {len(HARD)} finding(s)")
        return 1
    print("PERSISTENCE AUDIT OK — FT-01 call sites verified on boot, exit and tick paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
