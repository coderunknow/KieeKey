#!/usr/bin/env python3
"""Resolve rc1.hpp's profile samples (raw pc values) to functions and source lines.

    prof_sym.py --profile=benchmark/results/<c>/raw/profile_kieekey_as-shipped.jsonl \
                --bin=benchmark/.build/bench_prof --md=benchmark/results/<c>/logs/profile_kieekey_as-shipped.md

Ranking instrument only: ITIMER_PROF ticks every ~4 ms and bench_prof reserves a
frame pointer, so a share is meaningful to about one decimal place and NOTHING is
decided on it (PROTOCOL.md §9). Symbols and lines come from addr2line/nm on the
build that produced the samples — if the binary was rebuilt since, the resolution
is stale, and that is reported rather than papered over.
"""
import argparse
import bisect
import collections
import json
import os
import subprocess
import sys


def load_symbols(binary):
    out = subprocess.run(["nm", "-C", "--defined-only", "-n", binary],
                         capture_output=True, text=True).stdout.splitlines()
    addrs, names = [], []
    for line in out:
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[1] in ("t", "T", "W", "i"):
            try:
                a = int(parts[0], 16)
            except ValueError:
                continue
            addrs.append(a)
            names.append(parts[2].strip())
    return addrs, names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", required=True)
    ap.add_argument("--bin", default="benchmark/.build/bench_prof")
    ap.add_argument("--md", default="")
    ap.add_argument("--top", type=int, default=30)
    a = ap.parse_args()
    if not os.path.isfile(a.bin):
        sys.exit(f"[prof] {a.bin} is gone — the samples cannot be attributed to code that no longer exists")
    counts = collections.Counter()
    meta = {}
    with open(a.profile, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("{"):
                continue
            r = json.loads(line)
            if r.get("mode") == "profile-meta":
                meta = r
            elif r.get("mode") == "sample":
                counts[r.get("pc", 0)] += r.get("n", 1)
    total = sum(counts.values())
    if not total:
        sys.exit("[prof] no samples in the profile file")
    addrs, names = load_symbols(a.bin)
    by_fn = collections.Counter()
    pc_list = sorted(counts)
    resolved, unresolved = [], 0
    for pc, n in counts.items():
        i = bisect.bisect_right(addrs, pc) - 1 if addrs else -1
        # extent = up to the NEXT symbol, so a sample that falls past a small
        # function is not attributed to it because a fixed window happened to
        # cover the gap.
        if i < 0:
            by_fn["??"] += n
            unresolved += n
            continue
        end = addrs[i + 1] if i + 1 < len(addrs) else addrs[i] + 0x4000
        if i >= 0 and addrs[i] <= pc < end:
            by_fn[names[i]] += n
            resolved.append(n)
        else:
            by_fn["??"] += n
            unresolved += n
    # source lines for the top functions, when the binary carries debug info
    lines = collections.Counter()
    if pc_list:
        top = sorted(counts.items(), key=lambda kv: -kv[1])[:a.top]
        inp = "\n".join(hex(pc) for pc, _ in top)
        p = subprocess.run(["addr2line", "-f", "-C", "-e", a.bin],
                           input=inp, capture_output=True, text=True)
        out2 = p.stdout.splitlines()
        for k, (pc, n) in enumerate(top):
            fn = out2[2 * k] if 2 * k < len(out2) else "??"
            loc = out2[2 * k + 1] if 2 * k + 1 < len(out2) else "??:0"
            lines[f"{fn} @ {loc}"] += n
    keys = sorted(by_fn, key=lambda k: -by_fn[k])[:a.top]
    src = sorted(lines, key=lambda k: -lines[k])[:a.top]
    ns_per_key = (meta.get("window_ns", 0) / meta.get("keys", 1)) if meta.get("keys") else None
    eng = os.path.basename(meta.get("engine", "?")) if meta else "?"
    body = [f"# sampling profile — {os.path.basename(a.profile)}", ""]
    if meta:
        body.append(f"* engine column: `{meta.get('engine')}` · build `{meta.get('build')}` · "
                    f"stage `{meta.get('stage')}` · configuration `{meta.get('config')}`")
        body.append(f"* {total:,} samples at {meta.get('hz')} Hz over "
                    f"{meta.get('window_ns'):,} ns of timed work (ITIMER_PROF granularity "
                    f"here is the kernel tick, ~4 ms)")
        body.append(f"* keys processed in the window: {meta.get('keys'):,}"
                    + (f" → {ns_per_key:.1f} ns/key measured" if ns_per_key else ""))
        body.append(f"* unresolved sample count: {unresolved:,} ({unresolved / total * 100:.1f} %)")
    body += ["", "## top functions", "", "| samples | % | function |", "|---:|---:|---|"]
    for k in keys:
        body.append(f"| {by_fn[k]:,} | {by_fn[k] / total * 100:.1f} % | `{k}` |")
    body += ["", f"## top {a.top} source lines (of the hottest {a.top} pcs)", "",
             "| samples | % | location |", "|---:|---:|---|"]
    for k in src:
        body.append(f"| {lines[k]:,} | {lines[k] / total * 100:.1f} % | {k} |")
    body += ["", "_Ranking only. Decisions are made on `--mode=tput`, never here._"]
    txt = "\n".join(body) + "\n"
    if a.md:
        os.makedirs(os.path.dirname(a.md), exist_ok=True)
        with open(a.md, "w", encoding="utf-8") as f:
            f.write(txt)
        print(f"[prof] wrote {a.md} ({total:,} samples, {len(by_fn)} functions)")
    else:
        print(txt)
    return 0


if __name__ == "__main__":
    sys.exit(main())
