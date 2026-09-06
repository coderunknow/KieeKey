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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# File: scripts/check_version.py
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
"""KieeKey version-consistency guard.

Every release of this project has carried at least one stale version string.
The v1.2.0 audit found FOUR separate identifiers still claiming an older
release inside a build whose UI already said "1.2.0":

    src/app/KieeKeyApp.rc        FILEVERSION/PRODUCTVERSION 1,1,3,0
    src/app/KieeKeyApp.manifest  assemblyIdentity 1.1.3.0
    src/app/main.cpp             KieeKey_1.1.0_Singleton / KieeKey_1.1.0_Wake
    demo/main.cpp                "KieeKey v1.1.0 - engine demo"

A mixed version identifier is not cosmetic: it is how a user ends up unable to
answer "which build am I actually running?" from File Explorer, and how a bug
report gets filed against the wrong release.

This script fails (exit 1) when any carrier disagrees with the others.

Usage:
    scripts/check_version.py [--repo=DIR] [--expect=X.Y.Z]

It is wired into tests/run_all_tests.sh so the release gate cannot pass while
the identifiers are mixed.
"""

from __future__ import annotations

import argparse
import re
import sys
import pathlib

# The single source of truth. KieeKey ships "X.Y.Z" with an optional channel
# word; the PE VERSIONINFO needs a 4-part number, the manifest needs 4 parts,
# and the UI shows the 3-part form plus the channel.
DEFAULT_EXPECT = "1.2.2"
CHANNEL = "RC3"


def _fail(msg: str) -> None:
    print(f"[version] FAIL: {msg}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".", help="repository root")
    ap.add_argument("--expect", default=DEFAULT_EXPECT, help="expected X.Y.Z")
    args = ap.parse_args()

    root = pathlib.Path(args.repo)
    want = args.expect
    want4 = f"{want}.0"
    want_ui = f"{want} {CHANNEL}" if CHANNEL else want

    problems: list[str] = []

    def read(rel: str) -> str:
        p = root / rel
        if not p.exists():
            problems.append(f"{rel}: missing")
            return ""
        return p.read_text(encoding="utf-8")

    # ---- 1. CMake project() -------------------------------------------------
    cmake = read("CMakeLists.txt")
    if cmake:
        m = re.search(r"project\(\s*KieeKey\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmake)
        if not m:
            problems.append("CMakeLists.txt: no project(KieeKey VERSION …) found")
        elif m.group(1) != want:
            problems.append(f"CMakeLists.txt: project VERSION {m.group(1)} != {want}")

    # ---- 2. VERSIONINFO (numeric + strings) ---------------------------------
    rc = read("src/app/KieeKeyApp.rc")
    if rc:
        numeric = re.findall(r"(?:FILEVERSION|PRODUCTVERSION)\s+([0-9]+,\s*[0-9]+,\s*[0-9]+,\s*[0-9]+)", rc)
        for n in numeric:
            got = n.replace(" ", "")
            if got != want4.replace(".", ","):
                problems.append(f"KieeKeyApp.rc: {got} != {want4.replace('.', ',')}")
        for key in ("FileVersion", "ProductVersion"):
            m = re.search(rf'VALUE "{key}",\s*"?([0-9][0-9.]*)"?', rc)
            if m and m.group(1) != want4:
                problems.append(f"KieeKeyApp.rc: {key} {m.group(1)} != {want4}")

    # ---- 3. manifest assemblyIdentity ---------------------------------------
    manifest = read("src/app/KieeKeyApp.manifest")
    if manifest:
        m = re.search(r'assemblyIdentity version="([0-9][0-9.]*)"', manifest)
        if not m:
            problems.append("KieeKeyApp.manifest: no assemblyIdentity version")
        elif m.group(1) != want4:
            problems.append(f"KieeKeyApp.manifest: version {m.group(1)} != {want4}")

    # ---- 4. the app's own user-visible strings -------------------------------
    maincpp = read("src/app/main.cpp")
    if maincpp:
        m = re.search(r'kAppVersion\[\]\s*=\s*L"([^"]*)"', maincpp)
        if not m:
            problems.append("main.cpp: kAppVersion not found")
        elif m.group(1) != want:
            problems.append(f"main.cpp: kAppVersion {m.group(1)!r} != {want!r}")

        m = re.search(r'kAppVersionFull\[\]\s*=\s*L"([^"]*)"', maincpp)
        if not m:
            problems.append("main.cpp: kAppVersionFull not found")
        elif m.group(1) != want_ui:
            problems.append(f"main.cpp: kAppVersionFull {m.group(1)!r} != {want_ui!r}")

        m = re.search(r'kAppTitle\[\]\s*=\s*L"([^"]*)"', maincpp)
        if m and want_ui not in m.group(1):
            problems.append(f"main.cpp: kAppTitle {m.group(1)!r} does not contain {want_ui!r}")

        # The single-instance names must be VERSION-FREE: embedding a version
        # means an old and a new instance stop sharing the mutex, so both
        # install a keyboard hook and every keystroke is composed twice.
        for name in ("kSingletonMutexName", "kWakeEventName"):
            m = re.search(rf'{name}\[\]\s*=\s*L"([^"]*)"', maincpp)
            if m and re.search(r"[0-9]+\.[0-9]+", m.group(1)):
                problems.append(
                    f"main.cpp: {name} embeds a version number ({m.group(1)!r}) — "
                    "an upgrade would then allow two instances to run")

    # ---- 5. no stale version identifiers anywhere in shipped sources ---------
    # v1.2.1 Stable: the scan now covers the WinUI 3 front-end too — its
    # About line hardcoded "v1.2.0 Stable" through the whole 1.2.1 RC cycle
    # (found by the Stable release audit); the front-end now derives the
    # string from the public macros, and this scan keeps it covered.
    stale: list[str] = []
    for rel in ("src/app/main.cpp", "src/app/KieeKeyApp.rc",
                "src/app/KieeKeyApp.manifest", "src/app/resource.h",
                "src/ui/MainWindow.xaml.cpp",
                "demo/main.cpp", "CMakeLists.txt"):
        text = read(rel)
        if not text:
            continue
        for m in re.finditer(r"\b1\.1\.[0-9]\b|\b1\.0\.[0-9]\b|v1\.2\.0", text):
            line_no = text[:m.start()].count("\n") + 1
            line = text.splitlines()[line_no - 1].strip()
            # Historical "fixed in v1.1.x" comments are legitimate; only flag
            # lines that look like they are DECLARING a version.
            stripped = line.lstrip()
            # Comments are documentation: "fixed in v1.1.3" is history, not
            # an identifier. Only real declarations can be stale.
            if stripped.startswith("//") or stripped.startswith("*") or stripped.startswith("/*"):
                continue
            if re.search(r"VERSION|version|kApp|v1\.[01]\.[0-9]\s*[-—–]|\"|'", line) and \
               not re.search(r"v1\.[01]\.[0-9][-a-z]|fixed|since|was|old|legacy", line, re.I):
                stale.append(f"{rel}:{line_no}: {line[:100]}")
    problems.extend(stale)

    # ---- 6. the public C++ version macros (v1.2.1 RC3: this carrier was -----
    # missed by the RC2 release — the macro still said 1.2.0 while the app
    # shipped 1.2.1; the whole point of this script is that this class of
    # drift must fail the gate.
    corehpp = read("src/core/kieekey_core.hpp")
    if corehpp:
        m = re.search(r"#define\s+OPENKEY_KIEEKEY_VERSION_MAJOR\s+([0-9]+)", corehpp)
        n = re.search(r"#define\s+OPENKEY_KIEEKEY_VERSION_MINOR\s+([0-9]+)", corehpp)
        q = re.search(r"#define\s+OPENKEY_KIEEKEY_VERSION_PATCH\s+([0-9]+)", corehpp)
        if m and n and q:
            got = f"{m.group(1)}.{n.group(1)}.{q.group(1)}"
            if got != want:
                problems.append(f"kieekey_core.hpp: VERSION {got} != {want}")
        s = re.search(r'#define\s+OPENKEY_KIEEKEY_VERSION_STRING\s+"([^"]+)"', corehpp)
        if s and s.group(1) != want_ui:
            problems.append(f"kieekey_core.hpp: VERSION_STRING {s.group(1)!r} != {want_ui!r}")

    # ---- 7. the README headline (v1.2.2 RC3: the README still announced ----
    # "KieeKey v1.2.2 RC1" two RCs later, and its newest "What's new" section
    # was RC1 — the first thing every visitor reads was a stale release.
    readme = read("README.md")
    if readme:
        m = re.search(r"\*\*KieeKey v([0-9][0-9.]*(?: (?:Stable|RC[0-9]+))?)\*\* is a modern", readme)
        if not m:
            problems.append("README.md: headline '**KieeKey vX.Y.Z …** is a modern' not found")
        elif m.group(1) != want_ui:
            problems.append(f"README.md: headline version {m.group(1)!r} != {want_ui!r}")

        # The newest "What's new in vX" section must be THIS release.
        secs = re.findall(r"^## What's new in v([0-9][0-9.]*(?: (?:Stable|RC[0-9]+))?)",
                          readme, re.M)
        if not secs:
            problems.append("README.md: no \"What's new in v…\" section found")
        elif secs[0] != want_ui:
            problems.append(
                f"README.md: newest \"What's new\" section is {secs[0]!r}, "
                f"expected {want_ui!r} (the current release must be documented first)")

    # ---- 8. the demo banner (v1.2.2 RC3: demo/main.cpp hardcoded its own ---
    # "KieeKey v1.2.2 RC1 - engine demo" string and drifted two RCs behind).
    # It now derives from OPENKEY_KIEEKEY_VERSION_STRING; this check makes
    # re-hardcoding a gate failure rather than a silent regression.
    demo = read("demo/main.cpp")
    if demo:
        demo_code = "\n".join(l for l in demo.splitlines()
                              if not l.lstrip().startswith(("//", "*", "/*")))
        if "OPENKEY_KIEEKEY_VERSION_STRING" not in demo_code:
            problems.append(
                "demo/main.cpp: banner no longer derives from "
                "OPENKEY_KIEEKEY_VERSION_STRING (it must not hardcode a version)")
        # Any string literal carrying an X.Y[.Z] number is a hardcoded
        # version, including the split form L"KieeKey v" L"1.2.2 RC1".
        # Comments are documentation (this file's own history note quotes the
        # stale string on purpose), so only real code lines are scanned.
        for lineno, line in enumerate(demo.splitlines(), 1):
            if line.lstrip().startswith(("//", "*", "/*")):
                continue
            for m in re.finditer(r'L?"([^"\\]*[0-9]+\.[0-9]+(?:\.[0-9]+)?[^"\\]*)"', line):
                problems.append(
                    f"demo/main.cpp:{lineno}: hardcoded version in string literal "
                    f"{m.group(0)} — derive it from OPENKEY_KIEEKEY_VERSION_STRING instead")

    # ---- 9. license-header boilerplate must be VERSION-FREE -----------------
    # Every shipped file repeats a GPL header whose "Modified work:" line used
    # to name a release. Nobody bumps ~80 headers per release, so they rotted
    # into EIGHT different versions at once (1.1.0 … 1.2.2 RC3) inside a
    # single tree. The line is now version-free by policy; the banner comment
    # under it ("// KieeKey — <file>") likewise. Frozen reference snapshots
    # under tests/reference/ are historical artifacts and are exempt.
    boiler: list[str] = []
    exts = {".cpp", ".hpp", ".h", ".rc", ".py", ".sh", ".cmake", ".txt", ".yml", ".manifest"}
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix not in exts:
            continue
        rel = path.relative_to(root).as_posix()
        if rel.startswith((".git/", "docs/", "tests/reference/", "out/", "build/")):
            continue
        # .github/workflows/ is exempt: updating it requires a token with the
        # `workflows` scope, which routine doc/version maintenance does not
        # have, so a finding here would be unfixable in the normal flow. Its
        # header comment is cosmetic and carries no shipped identifier.
        if rel.startswith(".github/"):
            continue
        try:
            head = path.read_text(encoding="utf-8", errors="ignore")[:4000]
        except OSError:
            continue
        for m in re.finditer(
                r"^\s*(?://|#)\s*KieeKey v[0-9][0-9.]*(?: (?:Stable|RC[0-9]+))?\s*(?:-|—)",
                head, re.M):
            line = head[m.start():head.find("\n", m.start())].strip()
            boiler.append(f"{rel}: versioned header boilerplate — {line[:80]}")
    problems.extend(boiler)

    if problems:
        for p in problems:
            _fail(p)
        print(f"\n[version] {len(problems)} problem(s) — version identifiers are MIXED.",
              file=sys.stderr)
        return 1

    print(f"[version] OK — every carrier agrees on {want_ui} "
          f"(PE {want4}, manifest {want4}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
