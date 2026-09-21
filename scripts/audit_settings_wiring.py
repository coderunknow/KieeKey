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
# File: scripts/audit_settings_wiring.py
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
"""v1.3.0-beta5 (bug B9) — SOURCE-CONTRACT audit for the settings dialog.

THE BUG
    The tester reported "many options in the Bàn phím tab don't work".
    Root-causing that claim requires proving, for EVERY interactive
    control, that all three layers exist in source:

      1. WIRING    the control is created, its state is READ into the
                   option store (settingsFromControls / a click handler)
                   and REFLECTED back from the store on dialog open.
      2. LIVE-APPLY the control takes effect without needing a hidden
                   second step: either its own `case IDC_…:` (applies on
                   click, beta5 for tab 0) or a documented dedicated
                   Apply button / OK flow.
      3. EFFECT + PERSISTENCE the option the control writes is actually
                   consumed by engine code, and survives a restart
                   through a registry key written by saveSettings AND
                   read by loadSettings.

    beta4 failed layer 2 for the whole "Bàn phím" tab (tab-0 controls
    only applied via OK/Apply — flipping a checkbox and closing with X
    silently discarded it, while tabs 3/5/6/7 applied on click) and
    layer 3 for tabs 6-7 (Live-effects / Chaos / AI-opt-in were applied
    on click but NEVER persisted, so every restart reverted them).

HOW THIS AUDIT WORKS (and its limits)
    Pure source inspection — no Windows needed. For each interactive
    control id found in the kTabN arrays of src/app/main.cpp it checks:
      created   `HMENU>(ID)` creation site exists
      read      some occurrence of the ID sits within a window that
                contains a state-read verb (BM_GETCHECK, CB_GETCURSEL,
                IsDlgButtonChecked, dlgChecked, GetDlgItemInt,
                GetWindowText*). Window heuristics can over-credit an
                ID that merely NEIGHBORS a verb; the curated exemption
                table documents every ID that is intentionally covered
                by a group anchor instead of its own read/reflect site.
      reflect   same window scan with reflection verbs (CheckDlgButton,
                CB_SETCURSEL, SetDlgItemInt, CheckRadioButton,
                SetWindowTextW)
      apply     `case ID:` label exists (live-apply on click), or the ID
                is mapped to a dedicated apply control whose own case
                label exists, or to the OK/Apply flow (documented).
      persist   the mapped registry key appears in BOTH getDword and
                setDword form, or a documented exemption (session-only
                state, file-based persistence, dedicated savers).
      effect    the mapped option field is consumed by core engine
                sources (src/core, src/tsf) or, for app-layer state, by
                main.cpp outside the settings functions.
    The EFFECT layer's behavioral side is pinned separately by
    tests/test_settings_wiring.cpp (probe-verified A/B output pairs for
    every tab-0 option); this script pins the source contracts.

Exit 0 = every interactive control passes every applicable layer.
"""

import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(REPO, "src", "app", "main.cpp")

READ_VERBS = ("BM_GETCHECK", "CB_GETCURSEL", "IsDlgButtonChecked", "dlgChecked",
              "GetDlgItemInt", "GetDlgItemTextW", "GetWindowTextLengthW",
              "GetWindowTextW")
REFLECT_VERBS = ("CheckDlgButton", "CB_SETCURSEL", "SetDlgItemInt",
                 "CheckRadioButton", "SetWindowTextW")
WINDOW = 300  # chars of context around an ID occurrence

# IDs whose read/reflect happens through a group anchor (radio triads read
# two members and infer the third; CheckRadioButton names first+last only).
EXEMPT_READ = {
    "IDC_RADIO_SIMPLETELEX": "inferred: settingsFromControls reads TELEX/VNI, else-branch = SimpleTelex",
    "IDC_RADIO_OUT_SEND":    "inferred: handler reads OUT_AUTO/OUT_TSF, else-branch = SendInput",
    "IDC_RAD_DIAG_BASIC":    "inferred: handler compares wParam to OFF/FULL, else-branch = Basic",
}
EXEMPT_REFLECT = {
    "IDC_RADIO_VNI":      "group anchor: CheckRadioButton(TELEX, SIMPLETEX, TELEX+inputMethod)",
    "IDC_RADIO_OUT_TSF":  "group anchor: CheckRadioButton(OUT_AUTO, OUT_SEND, OUT_AUTO+outMode)",
}

# Live-apply layer: ID -> how it takes effect when it has no own case label.
# "ok" means the documented OK/Apply flow; otherwise the value is the ID of
# the dedicated apply control whose `case` label must exist.
APPLY_VIA = {
    "IDC_CMB_FAILMODE":       "IDC_BTN_APPLY_ARCADE_CFG",
    "IDC_EDT_RHYTHM_BPM":     "IDC_BTN_APPLY_ARCADE_CFG",
    "IDC_CMB_PASSAGE_LANG":   "IDC_BTN_APPLY_ARCADE_CFG",
    "IDC_CMB_STEERING":       "IDC_BTN_APPLY_ARCADE_CFG",
    "IDC_EDIT_MACRO":         "ok",
}

# Persistence layer: ID -> registry key that must appear as getDword AND
# setDword. None + reason = documented exemption.
PERSIST = {
    "IDC_RADIO_TELEX": "InputMethod", "IDC_RADIO_VNI": "InputMethod",
    "IDC_RADIO_SIMPLETELEX": "InputMethod",
    "IDC_COMBO_CODETABLE": "CodeTable",
    "IDC_CHK_DIGITS": "DigitsLiteral", "IDC_CHK_SPELL": "CheckSpelling",
    "IDC_CHK_RESTORE": "RestoreIfWrong", "IDC_CHK_QUICK": "QuickTelex",
    "IDC_CHK_MODERN": "ModernOrthography", "IDC_CHK_UPPER": "UpperCaseFirst",
    "IDC_CHK_MACRO": "UseMacro",
    "IDC_RADIO_OUT_AUTO": "OutputMode", "IDC_RADIO_OUT_TSF": "OutputMode",
    "IDC_RADIO_OUT_SEND": "OutputMode",
    "IDC_COMBO_PERF": "PerfProfile",
    "IDC_CHK_PERF_LOWCPU": "PerfHybrid", "IDC_CHK_PERF_DICT": "PerfHybrid",
    "IDC_CHK_NOTIFY": None,          # session mute BY DESIGN (no key)
    "IDC_CHK_EXCLUDE_IDE": "ExcludeIde", "IDC_CHK_EXCLUDE_GAME": "ExcludeGame",
    "IDC_CHK_EXCLUDE_SHELL": "ExcludeShell",
    "IDC_EDIT_MACRO": None,          # persisted to %APPDATA%\\KieeKey\\macros.txt
    "IDC_RAD_DIAG_OFF": None,        # persisted via saveDiagLevel()/loadDiagLevel()
    "IDC_RAD_DIAG_BASIC": None, "IDC_RAD_DIAG_FULL": None,
    "IDC_CMB_FAILMODE": "ArcadeFailMode", "IDC_EDT_RHYTHM_BPM": "ArcadeRhythmBpm",
    "IDC_CMB_PASSAGE_LANG": "ArcadePassageLang", "IDC_CMB_STEERING": "ArcadeSteering",
    "IDC_CHK_CHAOS_MASTER": "ChaosMaster", "IDC_CHK_CHAOS_CASE": "ChaosCase",
    "IDC_CHK_GLYPH_TRANSFORM": "ChaosGlyph",
    "IDC_CHK_LIVE": "LiveEnabled", "IDC_CHK_LIVE_CASE": "LiveCase",
    "IDC_CMB_LIVE_GLYPH": "LiveGlyph", "IDC_CMB_LIVE_INTENSITY": "LiveIntensity",
    "IDC_CHK_AI_OPTIN": "AiOptIn",
}
PERSIST_EXEMPT_MARKERS = {
    "IDC_EDIT_MACRO": (r"writeMacrosFile", "app"),
    "IDC_RAD_DIAG_OFF": (r"saveDiagLevel", "app"),
    "IDC_RAD_DIAG_BASIC": (r"saveDiagLevel", "app"),
    "IDC_RAD_DIAG_FULL": (r"saveDiagLevel", "app"),
    "IDC_CHK_NOTIFY": (r"setSessionMuted", "app"),
}

# Effect layer: ID -> (regex, scope). scope "core" = src/core + src/tsf
# sources; "app" = main.cpp (app-layer state consumed by the hook/UI paths).
EFFECT = {
    "IDC_RADIO_TELEX": (r"opts_\.inputMethod", "core"),
    "IDC_RADIO_VNI": (r"opts_\.inputMethod", "core"),
    "IDC_RADIO_SIMPLETELEX": (r"opts_\.inputMethod", "core"),
    "IDC_COMBO_CODETABLE": (r"opts_\.codeTable", "core"),
    "IDC_CHK_DIGITS": (r"digitsAreLiteral", "core"),
    "IDC_CHK_SPELL": (r"opts_\.checkSpelling", "core"),
    "IDC_CHK_RESTORE": (r"restoreIfWrongSpelling", "core"),
    "IDC_CHK_QUICK": (r"opts_\.quickTelex", "core"),
    "IDC_CHK_MODERN": (r"useModernOrthography", "core"),
    "IDC_CHK_UPPER": (r"upperCaseFirstChar", "core"),
    "IDC_CHK_MACRO": (r"opts_\.useMacro", "core"),
    "IDC_RADIO_OUT_AUTO": (r"g\.outputMode\.load", "app"),
    "IDC_RADIO_OUT_TSF": (r"g\.outputMode\.load", "app"),
    "IDC_RADIO_OUT_SEND": (r"g\.outputMode\.load", "app"),
    "IDC_COMBO_PERF": (r"applyPerfStrategy", "app"),
    "IDC_CHK_PERF_LOWCPU": (r"kHybridLowCpu", "core"),
    "IDC_CHK_PERF_DICT": (r"kHybridExtraCorrect", "core"),
    "IDC_CHK_NOTIFY": (r"sessionMuted", "app"),
    "IDC_CHK_EXCLUDE_IDE": (r"excludeIde", "core"),
    "IDC_CHK_EXCLUDE_GAME": (r"excludeGame", "core"),
    "IDC_CHK_EXCLUDE_SHELL": (r"excludeShell", "core"),
    "IDC_EDIT_MACRO": (r"applyMacrosText", "app"),
    "IDC_RAD_DIAG_OFF": (r"setLevel", "core"),
    "IDC_RAD_DIAG_BASIC": (r"setLevel", "core"),
    "IDC_RAD_DIAG_FULL": (r"setLevel", "core"),
    "IDC_CMB_FAILMODE": (r"rhythmFailMode", "core"),
    "IDC_EDT_RHYTHM_BPM": (r"rhythmBpm", "core"),
    "IDC_CMB_PASSAGE_LANG": (r"passageLanguage", "core"),
    "IDC_CMB_STEERING": (r"wasdSteering", "core"),
    "IDC_CHK_CHAOS_MASTER": (r"masterEnabled", "core"),
    "IDC_CHK_CHAOS_CASE": (r"randomCaseEnabled", "core"),
    "IDC_CHK_GLYPH_TRANSFORM": (r"glyphTransformEnabled", "core"),
    "IDC_CHK_LIVE": (r"g\.liveEffects\.", "app"),
    "IDC_CHK_LIVE_CASE": (r"g\.liveEffects\.", "app"),
    "IDC_CMB_LIVE_GLYPH": (r"g\.liveEffects\.", "app"),
    "IDC_CMB_LIVE_INTENSITY": (r"g\.liveEffects\.", "app"),
    "IDC_CHK_AI_OPTIN": (r"m_optIn", "core"),
}

INTERACTIVE = re.compile(r"_(CHK|RADIO|RAD|COMBO|CMB|EDT)_|^IDC_EDIT_MACRO$")
SKIP = re.compile(r"_(BTN|STAT|GRP|LNK)_")


def core_sources():
    files = []
    for pattern in ("src/core/*.cpp", "src/core/*.hpp", "src/tsf/*.cpp", "src/tsf/*.hpp"):
        files.extend(glob.glob(os.path.join(REPO, pattern)))
    return files


def window_has(text, cid, verbs):
    for m in re.finditer(r"\b%s\b" % cid, text):
        lo = max(0, m.start() - WINDOW)
        hi = min(len(text), m.end() + WINDOW)
        if any(v in text[lo:hi] for v in verbs):
            return True
    return False


def main():
    with open(MAIN, encoding="utf-8") as fh:
        main_src = fh.read()

    core_text = {}
    for path in core_sources():
        with open(path, encoding="utf-8", errors="replace") as fh:
            core_text[path] = fh.read()
    core_blob = "\n".join(core_text.values())

    # ---- collect interactive controls from the kTabN arrays ----
    controls = []
    seen = set()
    for tab, body in re.findall(r"kTab(\d+)\[\]\s*=\s*\{(.*?)\};", main_src, re.S):
        for cid in re.findall(r"\b(IDC_\w+)", body):
            if cid in seen or SKIP.search(cid) or not INTERACTIVE.search(cid):
                continue
            seen.add(cid)
            controls.append((tab, cid))

    if len(controls) < 25:
        print("AUDIT BROKEN: only %d interactive controls extracted" % len(controls))
        return 1

    failures = []
    print("%-4s %-26s %-8s %-8s %-8s %-8s %-8s %-8s" %
          ("tab", "control", "created", "read", "reflect", "apply", "persist", "effect"))
    for tab, cid in controls:
        row = []

        # Creation: either a literal HMENU cast site, or membership in the
        # kIds[] array that WM_CREATE loops over with mkCtl(..., HMENU(...)).
        created = re.search(r"HMENU>?\(\s*%s\s*\)" % cid, main_src) is not None
        if not created:
            for m in re.finditer(r"kIds\[\]\s*=\s*\{([^}]*)\}", main_src):
                if re.search(r"\b%s\b" % cid, m.group(1)):
                    tail = main_src[m.end():m.end() + 500]
                    if "mkCtl" in tail and "HMENU" in tail:
                        created = True
                        break
        row.append(created)

        if cid in EXEMPT_READ:
            row.append(True)
        else:
            # Radio handlers may read the ID through `wParam ==` comparisons
            # (BN_CLICKED delivers the control id in wParam) instead of a
            # state-query verb.
            #
            # v1.3.0-beta8: follow the hoisted-handle pattern too. A reader may
            # resolve the control ONCE into a local HWND and query that local
            # further down than the fixed context window reaches:
            #     const HWND langCtl = ::GetDlgItem(hwnd, IDC_CMB_PASSAGE_LANG);
            #     ...
            #     ::SendMessageW(langCtl, CB_GETCURSEL, 0, 0);
            # Without this, tryReadArcadeConfigFromDialog() — which reads the
            # fail-mode and passage-language combos exactly that way — was
            # reported as a missing read layer, i.e. a FALSE ALARM on a control
            # that is in fact fully wired. Credit the read only when the local
            # really is queried with a read verb, so a genuinely unread control
            # still fails.
            read_ok = (window_has(main_src, cid, READ_VERBS) or
                       re.search(r"wParam\s*==\s*%s\b" % cid, main_src) is not None)
            if not read_ok:
                for handle in re.findall(
                        r"\b(\w+)\s*=\s*::GetDlgItem\(\s*\w+\s*,\s*%s\s*\)" % cid, main_src):
                    if re.search(r"::SendMessageW\(\s*%s\s*,\s*(?:%s)\b"
                                 % (handle, "|".join(READ_VERBS)), main_src):
                        read_ok = True
                        break
            row.append(read_ok)

        if cid in EXEMPT_REFLECT:
            row.append(True)
        else:
            row.append(window_has(main_src, cid, REFLECT_VERBS))

        if re.search(r"case %s\s*:" % cid, main_src):
            row.append(True)
        elif cid in APPLY_VIA:
            via = APPLY_VIA[cid]
            row.append(via == "ok" or
                       re.search(r"case %s\s*:" % via, main_src) is not None)
        else:
            row.append(False)

        if cid not in PERSIST:
            row.append(False)   # unmapped control = audit gap by definition
        elif PERSIST[cid] is None:
            marker = PERSIST_EXEMPT_MARKERS.get(cid)
            row.append(marker is not None and
                       re.search(marker[0], main_src if marker[1] == "app" else core_blob)
                       is not None)
        else:
            key = PERSIST[cid]
            row.append(re.search(r'getDword\(L"%s"' % key, main_src) is not None and
                       re.search(r'setDword\(L"%s"' % key, main_src) is not None)

        if cid not in EFFECT:
            row.append(False)
        else:
            pattern, scope = EFFECT[cid]
            blob = main_src if scope == "app" else core_blob
            row.append(re.search(pattern, blob) is not None)

        marks = ["ok" if v else "FAIL" for v in row]
        print("%-4s %-26s %-8s %-8s %-8s %-8s %-8s %-8s" % (tab, cid, *marks))
        for name, ok in zip(("created", "read", "reflect", "apply", "persist", "effect"), row):
            if not ok:
                failures.append("%s: %s layer missing" % (cid, name))

    print()
    if failures:
        for f in failures:
            print("  " + f)
        print("AUDIT FAILED — %d layer gap(s) across %d interactive controls"
              % (len(failures), len(controls)))
        return 1
    print("AUDIT OK — %d interactive controls fully wired "
          "(created + read + reflected + live-apply + persisted + consumed)"
          % len(controls))
    return 0


if __name__ == "__main__":
    sys.exit(main())
