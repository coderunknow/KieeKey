#!/usr/bin/env python3
"""Regression/audit check for rc1_stats attribution accounting.

The check intentionally recomputes the rc1_gain source values from raw tput
artifacts instead of trusting summary.json.  It protects against the misleading
case where a table displays baseline/candidate medians but labels a paired
estimator as if it were their arithmetic difference.
"""
from __future__ import annotations

import argparse
import json
import statistics as st
from pathlib import Path
from typing import Any

BASE = "kieekey-base"
CAND = "kieekey-cand"
DEFAULT_CELL = "as-shipped|telex-end|prose"


def q(v: list[float], p: float) -> float | None:
    if not v:
        return None
    s = sorted(v)
    if len(s) == 1:
        return s[0]
    i = p * (len(s) - 1)
    lo = int(i)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (i - lo)


def load_rows(res: Path) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for p in sorted((res / "raw").glob("tput_s*.jsonl")):
        if "lead" in p.name:
            continue
        with p.open("r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if not line.startswith("{"):
                    continue
                try:
                    r = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if r.get("mode") == "tput" and not r.get("warm"):
                    r["_file"] = p.name
                    out.append(r)
    return out


def approx(a: float | None, b: float | None, eps: float = 0.015) -> bool:
    if a is None or b is None:
        return a is b
    return abs(a - b) <= eps


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--cell", default=DEFAULT_CELL)
    args = ap.parse_args()
    res = Path(args.results)
    summary = json.loads((res / "summary.json").read_text(encoding="utf-8"))
    rows = load_rows(res)

    by_cell: dict[str, dict[str, dict[tuple[Any, Any], float]]] = {}
    for r in rows:
        cell = "|".join(str(r.get(x)) for x in ("config", "method", "stream"))
        if r.get("engine") not in (BASE, CAND):
            continue
        by_cell.setdefault(cell, {}).setdefault(str(r["engine"]), {})[(r.get("session"), r.get("round"))] = float(r["engine_ns_per_key"])

    failures: list[str] = []
    example_line = ""
    for cell, per in sorted(by_cell.items()):
        if BASE not in per or CAND not in per:
            continue
        base_vals = list(per[BASE].values())
        cand_vals = list(per[CAND].values())
        common = sorted(set(per[BASE]) & set(per[CAND]))
        diffs = [per[CAND][k] - per[BASE][k] for k in common]
        base_med = st.median(base_vals)
        cand_med = st.median(cand_vals)
        median_improvement = base_med - cand_med
        median_improvement_pct = median_improvement / base_med * 100.0 if base_med else None
        paired_delta = st.median(diffs)
        paired_gain = -paired_delta
        paired_pct = paired_gain / base_med * 100.0 if base_med else None

        g = (summary.get("gain") or {}).get(cell)
        if not g:
            failures.append(f"{cell}: missing summary.gain row")
            continue
        checks = [
            ("base_median_ns", base_med),
            ("candidate_median_ns", cand_med),
            ("median_improvement_ns", median_improvement),
            ("median_improvement_pct", median_improvement_pct),
            ("paired_delta_median_ns", paired_delta),
            ("paired_gain_median_ns", paired_gain),
            ("paired_relative_effect_pct", paired_pct),
        ]
        for key, want in checks:
            if not approx(g.get(key), want):
                failures.append(f"{cell}: {key} summary={g.get(key)!r} raw={want!r}")
        if cell == args.cell:
            example_line = (
                f"[stats-consistency] example {cell}: "
                f"A diff-of-medians={median_improvement:.5f} ns; "
                f"B median-improvement%={median_improvement_pct:.5f}%; "
                f"C paired_delta_median={paired_delta:.5f} ns (cand-base); "
                f"D paired_relative_effect={paired_pct:.5f}% over baseline median; "
                f"base_median={base_med:.5f}; cand_median={cand_med:.5f}; pairs={len(common)}"
            )

    tables = (res / "tables.md").read_text(encoding="utf-8") if (res / "tables.md").exists() else ""
    ambiguous_headers = ["| cell | v1.2.2 ns/key | candidate ns/key | gain ns | gain %"]
    for hdr in ambiguous_headers:
        if hdr in tables:
            failures.append("tables.md still contains ambiguous rc1_gain header")
    if "median improvement ns" not in tables or "paired median gain ns" not in tables:
        failures.append("tables.md does not publish both descriptive and paired attribution estimators")
    if "rc1_gain_methodology" not in tables:
        failures.append("tables.md is missing the attribution methodology block")

    if example_line:
        print(example_line)
    if failures:
        for f in failures:
            print(f"[stats-consistency] FAIL: {f}")
        return 1
    print(f"[stats-consistency] OK: recomputed {len(by_cell)} cells from raw tput artifacts in {res}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
