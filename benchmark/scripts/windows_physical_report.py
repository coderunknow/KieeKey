#!/usr/bin/env python3
"""Build a Windows-physical-machine verdict report from benchmark artifacts.

This intentionally does not try to infer a real-PC result from Arena/VM data.
It consumes artifacts produced by run_windows_physical_benchmark.ps1 on the
user's own physical Windows host and writes a small, auditable report that
contains both the direct KieeKey-vs-UniKey verdict and the frozen-v1.2.2
regression baseline verdict.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import mean, pstdev
from typing import Any

DECIDING = "as-shipped|telex-end|prose"
SUBJECT = "kieekey"
CANDIDATE = "kieekey-cand"
RIVAL = "unikey-4.x"
BASE = "kieekey-base"


def load_json(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                rows.append({"_undecodable": line})
    return rows


def fmt(v: Any, digits: int = 2) -> str:
    if v is None:
        return "n/a"
    if isinstance(v, float):
        return f"{v:.{digits}f}"
    return str(v)


def cell_name(s: str) -> str:
    return s.replace("|", " · ")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True, help="benchmark/results/<campaign>")
    ap.add_argument("--out", default="", help="markdown report path; default: <results>/windows_physical_report.md")
    args = ap.parse_args()

    root = Path(args.results)
    summary = load_json(root / "summary.json", {})
    probe = load_json(root / "windows_host_probe.json", {})
    proc_rows = load_jsonl(root / "raw" / "windows_process_metrics.jsonl")

    cells = summary.get("cells", {})
    gain = summary.get("gain", {})
    verdict = summary.get("verdict", {})
    candidate = summary.get("candidate_verdict", {})
    diffab = summary.get("diffab_totals", {})
    robust = summary.get("robust_totals", {})
    memory = summary.get("memory", {})
    lat = summary.get("latency_cells", {})

    objective_pass = verdict.get("tier") in {"A", "B"}
    internal_pass = bool(candidate.get("accept"))
    correctness_pass = (diffab.get("per_key_mismatches") == 0 and
                        diffab.get("final_text_equal") is True)

    material_noise: list[str] = []
    for key in ("virtualization_warning", "power_warning", "defender_warning",
                "background_load_warning", "thermal_warning", "affinity_warning"):
        if probe.get(key):
            material_noise.append(str(probe[key]))

    lines: list[str] = []
    lines.append(f"# Windows physical benchmark report — {root.name}")
    lines.append("")
    lines.append("This report is valid only for the physical Windows host that produced these artifacts. ")
    lines.append("It must not be mixed with Arena/VM/Linux campaign numbers.")
    lines.append("")
    lines.append("## PASS/FAIL")
    lines.append("")
    lines.append(f"* Direct KieeKey NEW vs UniKey objective: **{'PASS' if objective_pass else 'FAIL'}** ({verdict.get('label', 'no verdict')}).")
    lines.append(f"* Frozen KieeKey v1.2.2 regression baseline: **{'PASS' if internal_pass else 'FAIL'}** ({candidate.get('decision', 'no decision')}).")
    lines.append(f"* Correctness identity against frozen baseline: **{'PASS' if correctness_pass else 'FAIL'}**; diffab mismatches={diffab.get('per_key_mismatches', 'n/a')} over {diffab.get('events', 'n/a')} events.")
    lines.append(f"* Material environment-noise flags: **{len(material_noise)}**.")
    lines.append("")
    if material_noise:
        lines.append("### Environment warnings")
        for w in material_noise:
            lines.append(f"* {w}")
        lines.append("")

    lines.append("## Host and run controls")
    lines.append("")
    for k in ("computer", "manufacturer", "model", "cpu", "logical_processors",
              "physical_cores", "os", "power_plan", "process_priority", "affinity_mask",
              "antivirus", "thermal_status", "background_cpu_before", "background_cpu_after"):
        if k in probe:
            lines.append(f"* {k}: `{probe[k]}`")
    lines.append("")
    meta = summary.get("meta", {})
    if meta:
        lines.append(f"* benchmark corpus: `{meta.get('corpus')}`")
        lines.append(f"* sessions × rounds: `{meta.get('sessions')} × {meta.get('rounds')}`")
        lines.append(f"* keys requested per stream: `{meta.get('keys')}`")
        lines.append(f"* words: `{meta.get('words')}`")
        lines.append("")

    lines.append("## Direct L1 same-process KieeKey NEW vs UniKey")
    lines.append("")
    lines.append("| cell | KieeKey NEW ns/key | UniKey ns/key | Δ ns | Δ % | 95% CI | paired rounds |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|")
    for name in sorted(cells):
        c = cells[name]
        subj = (c.get(SUBJECT) or c.get("kieekey") or {})
        rival = c.get(RIVAL) or {}
        d = c.get("d") or {}
        lines.append("| " + " | ".join([
            cell_name(name), fmt(subj.get("median")), fmt(rival.get("median")),
            fmt(d.get("median_d")), fmt(d.get("rel_pct")),
            f"{fmt(d.get('ci_lo'))}…{fmt(d.get('ci_hi'))}", fmt(d.get("n"), 0),
        ]) + " |")
    lines.append("")

    lines.append("## L2 latency distribution (median/p95/p99)")
    lines.append("")
    lines.append("| cell | engine | p50 ns | p95 ns | p99 ns | p99.9 ns | engine-core ns/key | rounds |")
    lines.append("|---|---|---:|---:|---:|---:|---:|---:|")
    for name in sorted(lat):
        for eng in (SUBJECT, CANDIDATE, RIVAL, BASE):
            row = lat[name].get(eng)
            if not row:
                continue
            p95 = row.get("p95_ns") if "p95_ns" in row else None
            lines.append("| " + " | ".join([
                cell_name(name), eng, fmt(row.get("p50_ns")), fmt(p95), fmt(row.get("p99_ns")),
                fmt(row.get("p999_ns")), fmt(row.get("engine_core_net_ns")), fmt(row.get("rounds"), 0),
            ]) + " |")
    lines.append("")

    lines.append("## Frozen KieeKey v1.2.2 baseline")
    lines.append("")
    lines.append("| cell | v1.2.2 ns/key | KieeKey NEW ns/key | gain ns | gain % | 95% CI | rounds |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|")
    for name in sorted(gain):
        g = gain[name]
        lines.append("| " + " | ".join([
            cell_name(name), fmt(g.get("base_ns")), fmt(g.get("cand_ns")), fmt(g.get("gain_ns")),
            fmt(g.get("gain_pct")), f"{fmt((g.get('ci') or [None, None])[0])}…{fmt((g.get('ci') or [None, None])[1])}",
            fmt(g.get("n"), 0),
        ]) + " |")
    lines.append("")

    if memory:
        lines.append("## Memory")
        lines.append("")
        lines.append("| engine | allocations | RSS MiB | soak keys |")
        lines.append("|---|---:|---:|---:|")
        for eng, m in sorted(memory.items()):
            lines.append(f"| {eng} | {fmt(m.get('allocs'), 0)} | {fmt(m.get('rss_mib'))} | {fmt(m.get('soak_keys'), 0)} |")
        lines.append("")

    if proc_rows:
        lines.append("## Windows per-process wall/CPU/memory samples")
        lines.append("")
        lines.append("| label | engine | iteration | wall ms | CPU ms | peak working set MiB | exit |")
        lines.append("|---|---|---:|---:|---:|---:|---:|")
        by_label: dict[str, list[float]] = {}
        for r in proc_rows:
            if "_undecodable" in r:
                continue
            label = str(r.get("label", ""))
            if isinstance(r.get("cpu_ms"), (int, float)):
                by_label.setdefault(label, []).append(float(r["cpu_ms"]))
            lines.append(f"| {label} | {r.get('engine','')} | {r.get('iteration','')} | {fmt(r.get('wall_ms'))} | {fmt(r.get('cpu_ms'))} | {fmt(r.get('peak_working_set_mib'))} | {r.get('exit_code','')} |")
        lines.append("")
        lines.append("### CPU-time variance by label")
        lines.append("")
        lines.append("| label | samples | mean CPU ms | σ CPU ms |")
        lines.append("|---|---:|---:|---:|")
        for label, vals in sorted(by_label.items()):
            lines.append(f"| {label} | {len(vals)} | {mean(vals):.2f} | {pstdev(vals):.2f} |")
        lines.append("")

    lines.append("## Raw artifacts")
    lines.append("")
    lines.append(f"* Raw JSONL directory: `{root / 'raw'}`")
    lines.append(f"* Tables: `{root / 'tables.md'}`")
    lines.append(f"* Summary JSON: `{root / 'summary.json'}`")
    lines.append(f"* Host probe JSON: `{root / 'windows_host_probe.json'}`")
    lines.append("")

    out = Path(args.out) if args.out else root / "windows_physical_report.md"
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"[windows-report] wrote {out}")
    print(f"[windows-report] direct objective: {'PASS' if objective_pass else 'FAIL'}")
    return 0 if (objective_pass and internal_pass and correctness_pass and not material_noise) else 1


if __name__ == "__main__":
    raise SystemExit(main())
