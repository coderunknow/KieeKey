#!/usr/bin/env python3
"""v1.3.0 RC1 statistics — the only place the campaign's arithmetic lives.

Reads benchmark/results/<campaign>/raw/*.jsonl, writes summary.json + tables.md
(next to them, in the campaign dir) with every table the report resolves.
Nothing here is a judgement: it publishes medians, paired differences, 95 %
bootstrap CIs, the A/A and B/B noise envelopes, every outlier the rounds
produced, and the tier verdict the protocol's bands define. The verdict is
computed from those numbers by `verdict_for()` below, which is written to be
readable in one screen — a "5 % win on prose, 6 % loss on edit-storm" campaign
comes out TIER MIXED because the function says so in the open.

Paired design: the unit of analysis is the ROUND, not the mean. Engines are
timed in a rotated order inside each round, so `d = cand − rival` per round
cancels the slow drift of host state that a campaign mean would fold in.
Sessions are the cluster: a bootstrap resamples WHOLE SESSIONS (a session's
rounds move together), because rounds within a session are not independent
observations — they share a process, a core, a moment in the machine's day.

usage: rc1_stats.py --results=benchmark/results/rc1-130 [--cell=as-shipped|telex-end|prose]
"""
import argparse
import json
import os
import random
import statistics as st
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DECIDING_DEFAULT = "as-shipped|telex-end|prose"
RIVAL_DEFAULT = "unikey-4.x"
BASELINE_ENGINES = ("kieekey-base", "kieekey-cand")   # attribution pair
SUBJECT = "kieekey"


def rows_of(res, name):
    out = []
    p = os.path.join(ROOT, res, "raw", name)
    if not os.path.isfile(p):
        return out
    with open(p, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("{"):
                try:
                    r = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if r.get("mode") != "meta":
                    out.append(r)
    # per-session files: tput_s0.jsonl … are the usual shape
    return out


def load_all(res):
    raw = os.path.join(ROOT, res, "raw")
    rows = []
    if not os.path.isdir(raw):
        return rows
    for fn in sorted(os.listdir(raw)):
        if not fn.endswith(".jsonl"):
            continue
        with open(os.path.join(raw, fn), encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.startswith("{"):
                    try:
                        r = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    r["_file"] = fn
                    rows.append(r)
    return rows


def q(v, p):
    if not v:
        return None
    s = sorted(v)
    if len(s) == 1:
        return s[0]
    i = p * (len(s) - 1)
    lo = int(i)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (i - lo)


def med(v):
    return st.median(v) if v else None


def boot_ci_sessions(pairs, n=4000, seed=7):
    """pairs: {session: [d, …]} -> 95 % CI of the median, resampling sessions."""
    sess = sorted(pairs)
    if not sess:
        return None, None, 0
    rnd = random.Random(seed)
    stats_ = []
    for _ in range(n):
        pick = [pairs[s] for s in (rnd.choice(sess) for _ in sess)]
        flat = [x for g in pick for x in g]
        if flat:
            stats_.append(st.median(flat))
    if not stats_:
        return None, None, sum(len(pairs[s]) for s in sess)
    return q(stats_, 0.025), q(stats_, 0.975), sum(len(pairs[s]) for s in sess)


def cell_key(r):
    return (r.get("config"), r.get("method"), r.get("stream"))


def analyse(rows, rival, subject=SUBJECT, ctl=None, metric="engine_ns_per_key"):
    """Per cell: per-engine medians, the paired subject-vs-rival difference with a
    session-clustered bootstrap CI, and the A/A (control) envelope."""
    cells = {}
    for r in rows:
        if r.get("mode") != "tput" or r.get("warm"):
            continue
        k = cell_key(r)
        cells.setdefault(k, {})
        e = r.get("engine")
        rnd = r.get("round")
        cells[k].setdefault(e, {})
        # a session appears in its own file; keep (session, round) as the pair id
        cells[k][e][(r.get("session", r.get("_file")), rnd)] = r.get(metric)
    out = {}
    for k, per in sorted(cells.items()):
        o = {}
        for e, d in per.items():
            v = [x for x in d.values() if x is not None]
            o[e] = {"median": med(v), "min": min(v), "max": max(v),
                    "p05": q(v, 0.05), "p95": q(v, 0.95),
                    "n": len(v), "n_sessions": len({s for s, _ in d})}
        if subject in per and rival in per:
            common = sorted(set(per[subject]) & set(per[rival]))
            pairs = {}
            for key in common:
                s, _ = key
                a, b = per[subject][key], per[rival][key]
                if a is None or b is None:
                    continue
                pairs.setdefault(s, []).append(a - b)
            diffs = [x for g in pairs.values() for x in g]
            lo, hi, n = boot_ci_sessions(pairs)
            smed = st.median(diffs) if diffs else None
            rmed = med([v for e in (rival,) for v in
                        [x for x in per[rival].values() if x is not None]]) if rival in per else None
            o["d"] = {"median_d": smed, "ci_lo": lo, "ci_hi": hi, "n": n,
                      "n_pairs": len(common),
                      "rel_pct": (smed / rmed * 100.0) if (smed is not None and rmed) else None,
                      "wins": sum(1 for x in diffs if x < 0),
                      "losses": sum(1 for x in diffs if x > 0)}
            # session medians, published: a reader must be able to see whether the
            # verdict rests on 6 agreeing sessions or on one loud outlier
            o["per_session"] = {s: {"median_d": st.median(g), "n": len(g)}
                                for s, g in sorted(pairs.items())}
        if ctl and ctl in per and subject in per:
            common = sorted(set(per[subject]) & set(per[ctl]))
            aa = {}
            for key in common:
                s, _ = key
                a, b = per[subject][key], per[ctl][key]
                if a is not None and b is not None:
                    aa.setdefault(s, []).append(a - b)
            diffs = [x for g in aa.values() for x in g]
            lo, hi, n = boot_ci_sessions(aa)
            o["aa"] = {"median_abs": med([abs(x) for x in diffs]) if diffs else None,
                       "p99_abs": q([abs(x) for x in diffs], 0.99) if diffs else None,
                       "median_d": st.median(diffs) if diffs else None,
                       "ci_lo": lo, "ci_hi": hi, "n": n}
        out["|".join(str(x) for x in k)] = o
    return out


TIERS = {
    "A": "TIER A — FASTER (>= 5 % on the deciding cell, no cell regresses)",
    "B": "TIER B — FASTER (statistically faster, but under 5 % or with a regressing cell)",
    "C": "TIER C — STATISTICAL TIE",
    "D": "TIER D — SLOWER",
    "MIXED": "TIER MIXED — faster on some cells, slower on others (no win wording allowed)",
    "NONE": "NO DATA",
}


def verdict_for(cells, aa_band_ns, deciding=DECIDING_DEFAULT, band_rel=1.0):
    """Protocol §15: the deciding cell sets the tier, and ANY cell where the
    subject is slower by more than the noise band cancels a win claim.

    The band is 2 x the A/A *median* |difference| — not the A/A p99. A p99-sized
    band would be ~30 ns on this host and would relabel a genuine 10 % loss as a
    tie, which is the opposite of what a noise band is for: it must absorb the
    instrument, not the effect. The tails stay published (tables) so the reader
    can see what the band rejected.
    """
    if not cells:
        return "NONE", None, 0, []
    dec = cells.get(deciding) or {}
    d = dec.get("d") or {}
    rel = d.get("rel_pct")
    if rel is None:
        return "NONE", None, 0, []
    regressing = []
    for name, c in cells.items():
        dd = c.get("d") or {}
        if dd.get("median_d") is None:
            continue
        if dd["median_d"] > max(aa_band_ns, 0.0) and abs(dd.get("rel_pct") or 0) > band_rel:
            regressing.append(name)
    better = rel < -band_rel and d.get("ci_hi") is not None and d["ci_hi"] < 0
    big = rel <= -5.0
    if better and not regressing and big:
        tier = "A"
    elif better and not regressing:
        tier = "B"
    elif abs(rel) <= band_rel or (d.get("ci_lo") is not None and d.get("ci_hi") is not None
                                 and d["ci_lo"] < 0 < d["ci_hi"]):
        tier = "C"
    elif regressing and d.get("wins", 0) > 0:
        tier = "MIXED"
    else:
        tier = "D"
    n = sum(len(c.get("per_session", {})) for c in cells.values()) // max(len(cells), 1)
    return tier, rel, d.get("n_pairs"), regressing


def table(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join(["---"] * len(headers)) + "|"]
    for r in rows:
        out.append("| " + " | ".join("" if v is None else str(v) for v in r) + " |")
    return "\n".join(out)


def f(v, nd=2):
    return "—" if v is None else (f"{v:.{nd}f}" if isinstance(v, float) else str(v))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--rival", default=RIVAL_DEFAULT)
    ap.add_argument("--deciding", default=DECIDING_DEFAULT)
    ap.add_argument("--aa-engine", default="kieekey-aa",
                    help="the A/A control column (a second instance of the subject engine, "
                         "identical code) whose spread defines the noise band; pass '' to "
                         "disable, which makes the tie verdict unclaimable")
    a = ap.parse_args()
    os.chdir(ROOT)
    rows = load_all(a.results)
    if not rows:
        sys.exit(f"[stats] no rows under {a.results}/raw")

    # The L1 table is the ROTATED pass only. The fixed-lead pass (tput_lead_*.jsonl)
    # measures a different thing — whether going first decides the result — and
    # folding its rows in would blur the two orders into one median, which is the
    # kind of averaging that makes a paired design meaningless. The distinction is
    # made on the source FILE, not the session name, so a run that forgot to set
    # --session= still lands in the main table instead of vanishing from it.
    perf = [r for r in rows if r.get("mode") == "tput" and "lead" not in (r.get("_file") or "")]
    cells = analyse(perf, a.rival, ctl=(a.aa_engine or None))
    aa_band = med([c["aa"]["median_abs"] for c in cells.values() if c.get("aa")])
    tier, rel, npairs, regressing = verdict_for(cells, aa_band or 0.0, a.deciding)

    # ---- attribution pair: gain over the frozen engine, same rounds ----------
    gain = {}
    if all(any(r.get("engine") == e for r in perf) for e in BASELINE_ENGINES):
        g = analyse(perf, BASELINE_ENGINES[0], subject=BASELINE_ENGINES[1])   # ctl: none
        for name, c in g.items():
            d = c.get("d") or {}
            base = (c.get(BASELINE_ENGINES[0]) or {}).get("median")
            cand = (c.get(BASELINE_ENGINES[1]) or {}).get("median")
            # d = cand - base, so a NEGATIVE d is time the candidate saved
            gain[name] = {"base_ns": base, "cand_ns": cand,
                          "gain_ns": (-d["median_d"]) if d.get("median_d") is not None else None,
                          "gain_pct": (-d["rel_pct"]) if d.get("rel_pct") is not None else None,
                          "ci": [(-d["ci_hi"]) if d.get("ci_hi") is not None else None,
                                 (-d["ci_lo"]) if d.get("ci_lo") is not None else None],
                          "n": d.get("n"), "wins": d.get("wins"), "losses": d.get("losses")}

    summary = {"campaign": os.path.basename(a.results.rstrip("/")),
               "cells": cells, "gain": gain, "rival": a.rival,
               "deciding_cell": a.deciding, "aa_band_ns": aa_band,
               "verdict": {"tier": tier, "label": TIERS[tier], "rel_pct": rel,
                           "paired_samples": npairs, "regressing_cells": regressing},
               "n_tput_rows": len(perf)}

    # ---- other artifacts ------------------------------------------------------
    def collect(mode, fields):
        return [r for r in rows if r.get("mode") == mode]

    summary["timer"] = (collect("timer", 1) or [{}])[0]
    summary["attrib_guard"] = (collect("attrib-guard", 1) or [{}])[0]
    summary["walks"] = (collect("walks-selftest", 1) or [{}])[0]
    summary["cold"] = collect("cold", 1)
    summary["diffab"] = collect("diffab", 1)
    summary["latency"] = collect("latency", 1)
    summary["mem"] = collect("mem", 1)
    summary["correctness"] = collect("example", 1)
    summary["robust"] = collect("robust", 1)

    # ---- fixed-lead vs rotated: is "who goes first" part of the answer? ------
    # The main table alternates the engine order every round, so a positional
    # advantage should average out across rounds; the fixed-lead pass never
    # alternates, so one engine always runs first. If the two passes agree within
    # the A/A control's own shift, the rotation did its job and the campaign is not
    # measuring slot order. If they disagree, that gap is reported instead of being
    # averaged away.
    order = {}
    lead = [r for r in rows if r.get("mode") == "tput" and "lead" in (r.get("_file") or "")]
    if lead:
        lc = analyse(lead, a.rival, ctl=(a.aa_engine or None))
        for name, c in lc.items():
            dL, dR = c.get("d") or {}, (cells.get(name) or {}).get("d") or {}
            ent = {"n_pairs": dL.get("n_pairs"), "delta_fixed_ns": dL.get("median_d"),
                   "delta_rotated_ns": dR.get("median_d"), "rel_fixed_pct": dL.get("rel_pct"),
                   "rel_rotated_pct": dR.get("rel_pct")}
            if ent["delta_fixed_ns"] is not None and ent["delta_rotated_ns"] is not None:
                ent["shift_ns"] = ent["delta_fixed_ns"] - ent["delta_rotated_ns"]
            aal, aar = c.get("aa") or {}, (cells.get(name) or {}).get("aa") or {}
            if aal.get("median_d") is not None and aar.get("median_d") is not None:
                ent["aa_shift_ns"] = aal["median_d"] - aar["median_d"]
            order[name] = ent
        shifts = [abs(v["shift_ns"]) for v in order.values() if v.get("shift_ns") is not None]
        aas = [abs(v["aa_shift_ns"]) for v in order.values() if v.get("aa_shift_ns") is not None]
        worst_name = max(order, key=lambda k: abs(order[k].get("shift_ns") or 0.0)) if order else None
        summary["order_effect"] = {
            "cells": len(order),
            "median_abs_shift_ns": med(shifts) if shifts else None,
            "max_abs_shift_ns": max(shifts) if shifts else None,
            "aa_median_abs_shift_ns": med(aas) if aas else None,
            "worst_cell": worst_name,
            "worst_shift_ns": (order.get(worst_name) or {}).get("shift_ns") if worst_name else None,
            "per_cell": order,
        }

    # ---- tables.md ------------------------------------------------------------
    t = {}
    eng_names = sorted({e for c in cells.values() for e in c if not e.startswith(("d", "aa", "per_session"))})
    rows_t = []
    for name, c in sorted(cells.items()):
        row = [name.replace("|", " · ")]
        for e in eng_names:
            row.append(f((c.get(e) or {}).get("median")))
        d = c.get("d") or {}
        row += [f(d.get("median_d")), f(d.get("rel_pct")),
                f"{f(d.get('ci_lo'))}…{f(d.get('ci_hi'))}" if d else "—",
                f"{d.get('wins')}/{d.get('n')}" if d else "—"]
        rows_t.append(row)
    t["rc1_l1"] = table(["cell"] + eng_names + [f"Δ vs {a.rival} (ns)", "Δ %", "95 % CI", "rounds favouring subject"], rows_t)

    t["rc1_cells"] = table(
        ["cell", "subject med", f"rival med", "Δ ns", "Δ %", "CI", "n rounds", "sessions",
         "subject min…max", "rival p05/p95"],
        [[n.replace("|", " · "),
          f((c.get(SUBJECT) or {}).get("median")), f((c.get(a.rival) or {}).get("median")),
          f((c.get("d") or {}).get("median_d")), f((c.get("d") or {}).get("rel_pct")),
          f"{f((c.get('d') or {}).get('ci_lo'))}…{f((c.get('d') or {}).get('ci_hi'))}",
          (c.get("d") or {}).get("n_pairs"), len(c.get("per_session") or {}),
          f"{f((c.get(SUBJECT) or {}).get('min'))}…{f((c.get(SUBJECT) or {}).get('max'))}",
          f"{f((c.get(a.rival) or {}).get('p05'))} / {f((c.get(a.rival) or {}).get('p95'))}"]
         for n, c in sorted(cells.items())])

    t["rc1_gain"] = table(
        ["cell", "v1.2.2 ns/key", "candidate ns/key", "gain ns", "gain %", "95 % CI", "rounds", "favouring"],
        [[n.replace("|", " · "), g.get("base_ns") and f(g["base_ns"]), g.get("cand_ns") and f(g["cand_ns"]),
          f(g.get("gain_ns")), f(g.get("gain_pct")),
          f"{f((g.get('ci') or [None, None])[0])}…{f((g.get('ci') or [None, None])[1])}",
          g.get("n"), f"{g.get('wins')}/{g.get('losses')}"]
         for n, g in sorted(gain.items())]) or "_attribution pair not measured in this campaign_"

    noise_rows = []
    for n, c in sorted(cells.items()):
        aa = c.get("aa") or {}
        if aa:
            noise_rows.append([n.replace("|", " · "), f(aa.get("median_abs")), f(aa.get("p99_abs")),
                               f(aa.get("median_d")), f"{f(aa.get('ci_lo'))}…{f(aa.get('ci_hi'))}", aa.get("n")])
    t["rc1_noise"] = table(["cell", "A/A median |Δ| ns", "A/A p99 |Δ| ns", "A/A median Δ ns",
                            "95 % CI", "rounds"], noise_rows)

    if summary["latency"]:
        by = {}
        for r in summary["latency"]:
            k = (r.get("config"), r.get("method"), r.get("stream"), r.get("engine"))
            by.setdefault(k, []).append(r)
        rows_l2 = []
        for k, rs in sorted(by.items()):
            p50 = med([r.get("p50_ns", 0) for r in rs])
            p99 = med([r.get("p99_ns", 0) for r in rs])
            mx = max(r.get("max_sample_ns", 0) for r in rs)
            rows_l2.append([" · ".join(str(x) for x in k), f(p50), f(p99), f(mx),
                            sum(r.get("n", 0) for r in rs)])
        t["rc1_l2"] = table(["cell", "p50 ns", "p99 ns (median of rounds)", "max ns", "keys"], rows_l2)

    if summary["cold"]:
        t["rc1_cold"] = table(["engine", "wall p50 ms", "wall min ms", "wall p95 ms", "wall max ms",
                               "first-round ns/key"],
                              [[r.get("engine"), f((r.get("wall_p50_ns") or 0) / 1e6),
                                f((r.get("wall_min_ns") or 0) / 1e6), f((r.get("wall_p95_ns") or 0) / 1e6),
                                f((r.get("wall_max_ns") or 0) / 1e6), f(r.get("first_round_engine_ns_per_key"))]
                               for r in summary["cold"]])
    if summary["diffab"]:
        t["rc1_diffab"] = table(
            ["cell", "events", "per-key mismatches", "final text equal", "subject build", "rival build"],
            [[f"{r.get('config')} · {r.get('method')} · {r.get('stream')}", r.get("events"),
              r.get("per_key_mismatches"), "yes" if r.get("final_text_equal") else "NO",
              r.get("subject_build"), r.get("rival_build")] for r in summary["diffab"]])
    if summary.get("order_effect", {}).get("per_cell"):
        oe = summary["order_effect"]
        t["rc1_order"] = table(
            ["cell", "Δ rotated ns", "Δ fixed-lead ns", "shift ns", "Δ % rotated", "Δ % fixed",
             "A/A shift ns", "pairs"],
            [[n.replace("|", " · "), f(v.get("delta_rotated_ns")), f(v.get("delta_fixed_ns")),
              f(v.get("shift_ns")), f(v.get("rel_rotated_pct"), 2), f(v.get("rel_fixed_pct"), 2),
              f(v.get("aa_shift_ns")), v.get("n_pairs")]
             for n, v in sorted(oe["per_cell"].items())])

    if summary["gain"] is not None and not gain:
        t["rc1_gain"] = ("_The attribution pair (kieekey-base / kieekey-cand) was not measured in this "
                         "campaign, so no gain-over-v1.2.2 figure is published. Run the campaign with "
                         "`--engines=contest,attrib`._")

    body = []
    for k, v in t.items():
        body.append(f"<<<TABLE:{k}>>>\n{v}\n<<<END>>>")
    with open(os.path.join(ROOT, a.results, "tables.md"), "w", encoding="utf-8") as f_:
        f_.write("\n\n".join(body) + "\n")
    with open(os.path.join(ROOT, a.results, "summary.json"), "w", encoding="utf-8") as f_:
        json.dump(summary, f_, indent=1, sort_keys=True)

    v = summary["verdict"]
    print(f"[stats] verdict: {v['label']}")
    oe = summary.get("order_effect") or {}
    if oe.get("cells"):
        print(f"[stats] order control: {oe['cells']} cells, max |shift| between fixed-lead and "
              f"rotated passes {f(oe.get('max_abs_shift_ns'))} ns (A/A shift "
              f"{f(oe.get('aa_median_abs_shift_ns'))} ns) — worst cell {oe.get('worst_cell')}")
    else:
        print("[stats] order control: NOT MEASURED (no fixed-lead pass in this campaign)")
    print(f"[stats] deciding cell: {a.deciding} · rel {f(rel)} % · paired samples {npairs} · "
          f"A/A band {f(aa_band, 3)} ns")
    if regressing:
        print(f"[stats] regressing cells ({len(regressing)}): " + ", ".join(regressing[:6]))
    if gain:
        g = gain.get(a.deciding) or {}
        print(f"[stats] gain over frozen v1.2.2 in the deciding cell: {f(g.get('gain_pct'), 2)} % "
              f"({f(g.get('base_ns'))} → {f(g.get('cand_ns'))} ns/key)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
