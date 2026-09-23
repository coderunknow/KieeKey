#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 coderunknow
r"""Structural gate for the UI probe's hand-built report (v1.3.0-beta8fix1, BS-19).

tools/ui_probe assembles ui_probe.json with `json += "...literal..." + number`.
That is fine until it is not: a dropped `+` between two adjacent string literals
is LEGAL C++ (the literals concatenate), so the compiler stays silent and the file
that reaches the workflow is not JSON at all. The workflow's ConvertFrom-Json then
throws, the step dies with a bare "Process completed with exit code 1", and the
run reports nothing about the dialog it just measured. That happened once, to this
very report, while the BS-19 fields were being added — CI run 35874350649 was
unusable and cost a Windows runner.

The probe now checks its own braces before writing (jsonBalanced) and the workflow
catches the parse error, but both of those only fire after CI has been paid for.
This gate is the local half: it reads the report's construction out of the SOURCE
and proves, without running anything, that

  1. every bracket opened inside the report's literals is closed again in the
     right order — which is exactly what the missing `+` broke (the keys after it
     landed inside an array that never closed before them);
  2. no key is emitted twice at the same nesting depth (a duplicated key is legal
     JSON and silently drops data in most parsers);
  3. the keys the workflow and the gates read are still present, at the depth they
     are read from — so a refactor cannot quietly delete the counters a green run
     is supposed to prove it ran.

The self-test at the end applies the two known-broken variants (the missing `+`
that hit CI, and a removed closing bracket) to an in-memory copy and requires the
gate to reject both. A gate that cannot fail is decoration.

Exit code 0 = the report's skeleton is sound AND the self-test passed; 1 = neither.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROBE = REPO_ROOT / "tools" / "ui_probe" / "ui_probe.cpp"

# Keys the workflow, the CI digest and the gates read, with the nesting depth
# they belong at (1 = the root object, 3 = one entry of "tabs"). Losing one of
# these is a silent loss of evidence — that is how "0 violations" could mean
# "the harness never ran".
REQUIRED_TOP = (
    "tool", "nativeDpi", "tabs", "controls", "checks", "findings",
    "screenCaptures", "screenUnavailable", "fuzzSteps", "fuzzChecks",
    "fuzzViolations", "scenarios", "scenarioViolations", "invariants",
    "traceLines", "passEntries", "handover", "firstViolations", "findingsByKind",
)
REQUIRED_TAB = (
    "tab", "scale", "controls", "dialog", "page", "scrollTravel",
    "screenshot", "findings", "geometry",
)

# C++ string literals, honouring escaped quotes/backslashes.
LITERAL_RE = re.compile(r'"(?:[^"\\]|\\.)*"')

# The edits that make the report unusable while still compiling.
#   variant 1 — the one that actually happened (CI run 35874350649): the two
#               literals concatenated, the passEntries array opened before the key
#               that follows it, and that key landed INSIDE the array; the report
#               was valid C++ and invalid JSON (and no key-depth check saw it).
#   variant 2 — a closing bracket deleted: the shape the bracket walk exists for.
BROKEN_VARIANTS = (
    (
        '            ",\\n \\"traceLines\\": " + std::to_string(g_traceLines) +\n'
        '            ",\\n \\"passEntries\\": [";',
        '            ",\\n \\"passEntries\\": [" +\n'
        '            ",\\n \\"traceLines\\": " + std::to_string(g_traceLines);',
    ),
    (
        '    json += "],\\n \\"handover\\": [";\n'
        '    // v1.3.0-beta8fix1 (bug BS-19): the handover notes and one example state per',
        '    // v1.3.0-beta8fix1 (bug BS-19): the handover notes and one example state per',
    ),
)


def cpp_unescape(literal_body: str) -> str:
    """Turn the inside of a C++ literal into the text it stands for."""
    out: list[str] = []
    i = 0
    while i < len(literal_body):
        ch = literal_body[i]
        if ch != "\\" or i + 1 >= len(literal_body):
            out.append(ch)
            i += 1
            continue
        nxt = literal_body[i + 1]
        out.append({"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}.get(nxt, nxt))
        i += 2
    return "".join(out)


def strip_comments(source: str) -> str:
    return re.sub(r"/\*.*?\*/", "", re.sub(r"//[^\n]*", "", source), flags=re.S)


def report_region(source: str) -> str:
    """The source between the report's first line and the file write."""
    return source[source.index('json += "{\\n \\"tool\\"'):
                  source.index("const std::wstring jsonPath")]


def literals_of(source: str) -> list[str]:
    return [cpp_unescape(m.group(0)[1:-1]) for m in LITERAL_RE.finditer(source)]


def analyse(text: str) -> tuple[list[str], dict[tuple[str, int], int], int]:
    """Walk the assembled report the way a parser would.

    Returns (problems, occurrences keyed by (name, depth), highest depth seen).
    JSON string literals are tracked exactly (the C++ side encodes them as \\",
    decoded before this runs), so a bracket inside a string is content, and a name
    only counts as a key when a ':' follows it.
    """
    problems: list[str] = []
    stack: list[str] = []
    keys: dict[tuple[str, int], int] = {}
    max_depth = 0
    i = 0
    line = 1
    while i < len(text):
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch == '"':
            j = i + 1
            body: list[str] = []
            while j < len(text):
                if text[j] == "\\" and j + 1 < len(text):
                    body.append(text[j + 1])
                    j += 2
                    continue
                if text[j] == '"':
                    break
                if text[j] == "\n":
                    line += 1
                body.append(text[j])
                j += 1
            name = "".join(body)
            k = j + 1
            while k < len(text) and text[k] in " \n\t":
                k += 1
            if k < len(text) and text[k] == ":":      # it is an object key
                keys[(name, len(stack))] = keys.get((name, len(stack)), 0) + 1
            i = j + 1
            continue
        if ch in "{[":
            stack.append(ch)
            max_depth = max(max_depth, len(stack))
            i += 1
            continue
        if ch in "}]":
            if not stack:
                problems.append(
                    f"line {line}: a closing {ch!r} with nothing open — a missing "
                    f"'+' between two adjacent literals makes keys land outside "
                    f"their object")
                i += 1
                continue
            opened = stack.pop()
            if (opened, ch) not in (("{", "}"), ("[", "]")):
                problems.append(f"line {line}: {ch!r} closes the {opened!r} opened "
                                f"earlier — the report's nesting is broken")
            i += 1
            continue
        i += 1
    if stack:
        problems.append(f"{len(stack)} bracket(s) are never closed (outermost "
                        f"{stack[0]!r}) — a missing '+' between two adjacent "
                        f"literals produces exactly this")
    return problems, keys, max_depth


def check_source(source: str) -> tuple[list[str], dict[tuple[str, int], int], int]:
    """All structural problems of one probe source text (empty list = sound)."""
    code = strip_comments(source)
    assembled = "".join(literals_of(report_region(code)))
    problems, keys, max_depth = analyse(assembled)

    for (name, depth), count in sorted(keys.items()):
        if count > 1:
            problems.append(f'the key "{name}" is emitted {count} times at depth '
                            f"{depth} — a duplicated key silently drops data in most "
                            f"parsers, and at depth 1 it means the report is not "
                            f"shaped the way the workflow reads it")
    for key in REQUIRED_TOP:
        if (key, 1) not in keys:
            problems.append(f'the top-level key "{key}" is no longer in the report — '
                            f"the CI digest and the gates read it")
    for key in REQUIRED_TAB:
        if (key, 3) not in keys:
            problems.append(f'the per-tab key "{key}" (depth 3) is no longer in the '
                            f"report — the per-tab annotations read it")
    return problems, keys, max_depth


def selftest(source: str) -> int:
    """Prove the gate can fail (a gate that cannot fail is not a gate)."""
    for index, (from_text, to_text) in enumerate(BROKEN_VARIANTS, start=1):
        if from_text not in source:
            print(f"[probe-json] FAIL: self-test variant {index} no longer matches the "
                  f"probe source — this gate has gone blind", file=sys.stderr)
            return 1
        broken_problems, _, _ = check_source(source.replace(from_text, to_text, 1))
        if not broken_problems:
            print(f"[probe-json] FAIL: the gate PASSES self-test variant {index} "
                  f"(a known-broken report) — it cannot protect anything",
                  file=sys.stderr)
            return 1
    if check_source(source)[0]:
        print("[probe-json] FAIL: the self-test's control case (the real source) does "
              "not pass", file=sys.stderr)
        return 1
    print("[probe-json] self-test OK — the gate rejects all "
          f"{len(BROKEN_VARIANTS)} known-broken variants (including the missing '+' "
          "that killed CI run 35874350649) and accepts the real report")
    return 0


def main() -> int:
    if not PROBE.is_file():
        print(f"[probe-json] FAIL: {PROBE} not found", file=sys.stderr)
        return 1
    source = PROBE.read_text(encoding="utf-8")
    problems, keys, max_depth = check_source(source)

    if problems:
        print("[probe-json] FAIL: the UI probe's report skeleton is broken")
        for problem in problems:
            print(f"  - {problem}")
        print("  (the report is built with `json += ...`; a missing '+' between two "
              "literals still compiles — this gate is what catches it before a "
              "Windows runner does)")
        return 1

    top = sum(1 for (_, depth) in keys if depth == 1)
    print(f"[probe-json] OK — brackets balanced, {max_depth} levels deep, {top} "
          f"top-level keys, {len(REQUIRED_TOP)} required root keys and "
          f"{len(REQUIRED_TAB)} required per-tab keys present")
    # The gate proves it can fail on every run, not just on the day it was written.
    return selftest(source)


if __name__ == "__main__":
    sys.exit(main())
