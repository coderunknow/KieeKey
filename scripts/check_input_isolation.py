#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 coderunknow
"""Architecture gate: game/Lab singletons must not run in the IME hook.
The explicit LiveEffects adapter is allowed: bounded one-to-one mapping,
separate opt-in, atomic configuration and deterministic rewrite tests.

This is intentionally a source-level dependency check, not a substitute for
Windows input testing. Core game tests cannot catch routing errors in main.cpp.
Beta1 passed those tests while globally swallowing keys in background games.
"""
import argparse
from pathlib import Path
import re
import sys


def function_body(source, signature):
    # Strip comments and literals so their braces/names do not affect the gate.
    source = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    '', source, flags=re.S)
    match = re.search(signature + r'\s*\{', source)
    if not match:
        raise ValueError(f"missing function: {signature}")
    start = match.end()
    depth = 1
    for i in range(start, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if depth == 0:
            return source[start:i]
    raise ValueError("unbalanced function body")


def check(root):
    source = (root / 'src/app/main.cpp').read_text(encoding='utf-8')
    producer = function_body(source, r'PD onHookEventImpl\(const KeyEvent& ev\)')
    problems = []
    for forbidden in ('ArcadeManager', 'ChaosEngine', 'ArcadeWindow', 'ChaosLabWindow'):
        if forbidden in producer:
            problems.append(f"{forbidden} reached from the global IME producer")
    if not re.search(r'if\s*\(ev.source\s*==\s*EventSource::Keyboard\s*&&\s*ownWindowHasFocus\(\)\)', producer):
        problems.append('own-window bypass must be limited to keyboard events; keep foreground bookkeeping')
    # Both literal and consumed engine output must reach the visual adapter;
    # reintroducing a replacement-only transform corrupts random-case continuity.
    #
    # v1.3.0-beta3: that decision was EXTRACTED from this producer into
    # ok::effects::planOutput() (src/core/LiveEffects.hpp) so the shipped hook
    # and tests/test_live_output_plan.cpp drive ONE tested implementation
    # instead of drifting copies. The gate now asserts (a) the producer routes
    # live output through planOutput, (b) planOutput still covers BOTH literal
    # insertion (rewrite(0, …)) and consumed engine rewrites
    # (rewrite(engineBackspace, …)), and (c) the routing precedes the
    # pass-through decision.
    effects = (root / 'src/core/LiveEffects.hpp').read_text(encoding='utf-8')
    if 'planOutput(' not in producer:
        problems.append('producer must route live output through ok::effects::planOutput')
    if 'fx.rewrite(0, text)' not in effects or 'fx.rewrite(engineBackspace, text)' not in effects:
        problems.append('live output adapter must cover literal insertion and engine rewrites')
    if producer.rfind('planOutput(') > producer.find('if (!suppress)'):
        problems.append('live output must precede pass-through decision')
    ownership = function_body(source, r'bool ownWindowHasFocus\(\) noexcept')
    if 'GetWindowThreadProcessId' not in ownership or 'GetCurrentProcessId' not in ownership:
        problems.append('own-window detection must use process ownership, not racing UI HWND fields')
    # Every control created in the optional settings pages must belong to
    # exactly one visibility list. Missing IDs/tab entries overlaid Arcade
    # widgets on the IME options and macro editor in beta1.
    tabs = re.findall(r'static constexpr int kTab[0-8]\[\]\s*=\s*\{(.*?)\};', source, re.S)
    tab_ids = re.findall(r'\bIDC_\w+', '\n'.join(tabs))
    start = source.index('// ---- tab 5:')
    end = source.index('// ---- buttons ----', start)
    for control in re.findall(r'mkCtl\(.*?\);', source[start:end], re.S):
        match = re.search(r'reinterpret_cast<HMENU>\((IDC_\w+)\)', control)
        if match is None:
            problems.append('optional settings page has an anonymous control that cannot hide with its tab')
        elif tab_ids.count(match.group(1)) != 1:
            problems.append(f'{match.group(1)} must belong to exactly one settings tab')
    return problems


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()
    try:
        errors = check(args.repo)
    except (OSError, ValueError) as exc:
        errors = [str(exc)]
    for error in errors:
        print(f'[input isolation] FAIL: {error}', file=sys.stderr)
    if errors:
        sys.exit(1)
    print('[input isolation] OK — game/Lab isolation + explicit live-output routes')
