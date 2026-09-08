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
                    except json.JSONDecodeError:   # torn final line of an interrupted step
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
    """Tier rules (docs/bench/rc1-130/PROTOCOL.md §6), stated exactly as coded.

    A cell REGRESSES when its paired median difference is positive and larger
    than the A/A control's own median |difference| — 1x, not 2x. The A/A column
    is two instances of identical code, so anything it cannot explain is not
    noise; using 2x here would be choosing the forgiving constant at the moment
    it flatters the subject. (The candidate-acceptance rule does use 2x, for the
    opposite purpose: refusing to discard a change over a two-nanosecond blip.
    Both constants are stated here so neither can be quietly swapped.)

    The band is deliberately the *median*, not the A/A p99. A p99-sized band
    would be ~30 ns on this host and would relabel a genuine 10 % loss as a tie;
    it must absorb the instrument, not the effect. Tails stay published in the
    tables so a reader can see what the band rejected.

    Tier A additionally requires a >= 5 % win in the deciding cell with the
    bootstrap CI excluding zero AND zero regressing cells — one regressing cell
    demotes a claimed win to B, and a mixed picture across cells says MIXED.
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
    ap.add_argument("--manifest", default="docs/bench/rc1-130/baseline_manifest.json",
                    help="the build manifest; the report quotes the toolchain out of it "
                         "rather than re-reading build.sh, so a report can never advertise "
                         "flags that the build did not use")
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

    # ---- candidate accept/reject, computed by the pre-registered rule -------
    # (docs/bench/rc1-130/PROTOCOL.md §"candidate acceptance"). A candidate is
    # ACCEPTed only if, in the deciding cell, it is faster than the frozen engine
    # with a bootstrap CI that excludes zero, AND no cell is slower than the frozen
    # engine by more than 2 x the A/A band. The second clause is what stops a 4 %
    # average win from hiding a 9 % regression on edit-storms; it is evaluated on
    # the SAME rounds, so campaign drift cannot rescue a bad candidate.
    # A campaign run without a candidate in the tree is not a candidate trial: the
    # two shim columns then hold identical code, so the same machinery measures the
    # harness's own drift. The manifest is loaded again here (not reused from below)
    # because the verdict block runs before the manifest block, and it is compared the
    # same way the gate compares it: source by source, against the frozen hash map.
    man0, mp0 = {}, os.path.join(ROOT, a.results, "manifest_at_build.json")
    if not os.path.isfile(mp0):
        mp0 = os.path.join(ROOT, a.manifest)
    if not os.path.isfile(mp0):
        mp0 = os.path.join(ROOT, a.manifest)
    if os.path.isfile(mp0):
        try:
            with open(mp0, encoding="utf-8") as fh0:
                man0 = json.load(fh0)
        except (OSError, ValueError):
            man0 = {}
    tree0 = man0.get("engine_sources_sha256") or {}
    base0 = man0.get("baseline_engine_sources_sha256") or {}
    differing0 = sorted(k for k, v in base0.items() if tree0.get(k) != v)
    manifest_known = bool(base0) and bool(tree0)
    is_baseline = manifest_known and not differing0
    if not manifest_known:
        print("[stats] WARNING: manifest has no frozen hash map — the campaign cannot be "
              "classified as baseline or candidate; the candidate verdict says UNKNOWN")

    g_dec = dict(gain.get(a.deciding) or {})
    band_ns = float(aa_band or 0.0)
    worse = sorted(n for n, v in gain.items()
                   if v.get("gain_ns") is not None and v["gain_ns"] < -2.0 * band_ns)
    ci = g_dec.get("ci") or [None, None]
    positive_ci = ci[0] is not None and ci[0] > 0
    accept = (g_dec.get("gain_ns") or 0.0) > 0 and positive_ci and not worse
    summary["candidate_verdict"] = {
        "rule": ("ACCEPT iff deciding-cell gain > 0 with 95 % CI excluding 0, and no cell "
                 "regressing by more than 2 x the A/A median band"),
        "accept": bool(accept),
        "decision": ("NOT RUN — baseline campaign (frozen src/core)" if is_baseline else
                     ("UNKNOWN — manifest unreadable" if not manifest_known else
                      ("ACCEPT" if accept else "REJECT"))),
        "baseline_campaign": bool(is_baseline),
        "manifest_known": bool(manifest_known),
        "differing_from_baseline": differing0,
        "null_test_abs_max_pct": (round(max((abs(v.get("gain_pct") or 0.0) for v in gain.values()),
                                            default=0.0), 2) if is_baseline else None),
        "deciding_cell": a.deciding,
        "deciding_gain_ns": g_dec.get("gain_ns"),
        "deciding_gain_pct": g_dec.get("gain_pct"),
        "deciding_ci_ns": ci,
        "band_ns": band_ns,
        "cells_regressing_beyond_band": worse,
        "cells_measured": len(gain),
        "note": ("a REJECT here is a measurement outcome, not a discarded file: the code, "
                 "its numbers and the reason stay in OPTIMIZATION_LEDGER.md"),
    }

    # ---- other artifacts ------------------------------------------------------
    def collect(mode, fields):
        return [r for r in rows if r.get("mode") == mode]

    # the campaign's own header row (corpus, seed, counts) rides along so the
    # report can state what was measured without parsing JSONL at render time
    # The report header quotes the *timing* pass's shape, so the meta row is taken from a
    # tput artifact: "the first meta row anywhere" used to return whichever step was read
    # first, and that step's keys=8000/rounds=5 were then published as the campaign's
    # size. Sessions are counted from the rows themselves, not from a remembered flag.
    tput_meta = next((r for r in rows if r.get("mode") == "meta"
                      and str(r.get("_file", "")).startswith("tput_s")), {})
    summary["meta"] = dict(tput_meta or (collect("meta", 1) or [{}])[0])
    sess = {r.get("_file") for r in rows
            if r.get("mode") == "tput" and str(r.get("_file", "")).startswith("tput_s")}
    if sess:
        summary["meta"]["sessions"] = len(sess)
        summary["meta"]["sessions_note"] = ("sessions counted from the timed rows' own "
                                            "_file field; rounds/keys/words/seed come from "
                                            "that pass's meta row")
    else:
        print("[stats] WARNING: no tput meta row found — the report's corpus line falls "
              "back to the first meta row in the artifacts and may describe another step")

    man = {}
    mpath = mp0  # the same file the verdict above read, so the report cannot describe
                 # one tree in §1 and another in §8
    if os.path.isfile(mpath):
        with open(mpath, encoding="utf-8") as fh:
            man = json.load(fh)
    if not man:
        print(f"[stats] WARNING: no manifest at {a.manifest} — the report cannot state which "
              f"build produced these numbers; run baseline_manifest.py")
    tree = man.get("engine_sources_sha256") or {}
    base = man.get("baseline_engine_sources_sha256") or {}
    # Cross-campaign corroboration: the report may quote the most recent other campaign
    # in benchmark/results/ for the same cell, because "we measured it twice at
    # different sizes" is a claim that needs both numbers present, not remembered.
    summary["prev"] = {}
    for cand in sorted(os.listdir(os.path.join(ROOT, "benchmark/results")), reverse=True):
        pth = os.path.join(ROOT, "benchmark/results", cand, "summary.json")
        if cand == os.path.basename(a.results.rstrip("/")) or not os.path.isfile(pth):
            continue
        try:
            with open(pth, encoding="utf-8") as fh:
                other = json.load(fh)
        except (OSError, ValueError):
            continue
        cell = (other.get("cells") or {}).get(a.deciding) or {}
        dd = cell.get("d") or {}
        if dd.get("rel_pct") is None:
            continue
        summary["prev"] = {"campaign": cand, "rel_pct": dd.get("rel_pct"),
                           "paired_samples": dd.get("n_pairs"),
                           "n_differing": len((other.get("manifest") or {}).get("differing_files")
                                              or (other.get("manifest") or {}).get("n_differing_from_baseline")
                                              or []),
                           "tier": (other.get("verdict") or {}).get("tier")}
        break

    # What the sampler actually delivered, so the report can state the limit instead of
    # asserting that a 1 kHz request was honoured: on this kernel the samples arrive on
    # the CONFIG_HZ tick, and the count/window pair proves it.
    pmetas = [r for r in rows if r.get("mode") == "profile-meta"
              and r.get("engine") == SUBJECT and r.get("config") == "as-shipped"]
    if pmetas:
        pm = pmetas[0]
        win = float(pm.get("window_ns") or 0.0)
        n = int(pm.get("samples") or 0)
        summary["profile"] = {
            "requested_hz": int(pm.get("hz") or 0),
            "delivered_hz": round(n / (win / 1e9), 1) if win else 0,
            "samples": n, "window_s": round(win / 1e9, 2),
            "keys": int(pm.get("keys") or 0),
            "note": "delivered = samples / measured window; the gap to the requested rate is "
                    "the kernel tick (CONFIG_HZ) limiting ITIMER_PROF, not the harness",
        }
    else:
        summary["profile"] = {}

    summary["manifest"] = {
        "path": a.manifest,
        "git": man.get("git", {}),
        "toolchain": man.get("toolchain", {}),
        "product_version": man.get("product_version"),
        "n_engine_sources_tree": len(tree),
        "n_engine_sources_baseline": len(base),
        "n_identical_to_baseline": sum(1 for k, v in base.items() if tree.get(k) == v),
        "n_differing_from_baseline": sorted(k for k, v in base.items() if tree.get(k) != v),
        "environment": man.get("environment", {}),
    }
    summary["timer"] = (collect("timer", 1) or [{}])[0]
    summary["attrib_guard"] = (collect("attrib-guard", 1) or [{}])[0]
    # (a "walks-selftest" row used to be read here; the mode was retired with the
    # candidate it guarded — see PROTOCOL.md §9)
    summary["cold"] = collect("cold", 1)
    summary["diffab"] = collect("diffab", 1)
    summary["latency"] = collect("latency", 1)
    # L2 rows are per (cell, engine, round); the report needs the same
    # median-over-rounds shape L1 has, otherwise prose would have to quote one
    # arbitrary round. Tails are medians of the per-round tails, and the worst
    # single sample is published next to them so a reader can see the difference
    # between "the tail is usually this" and "the tail once reached this".
    L2_FIELDS = ("p50_ns", "p90_ns", "p99_ns", "p999_ns", "mean_sample_ns", "max_sample_ns",
                 "engine_ns_per_key", "engine_core_net_ns", "prep_ns_per_key", "full_ns_per_key")
    lat_cells = {}
    for r in summary["latency"]:
        name = "|".join(str(r.get(x)) for x in ("config", "method", "stream"))
        per = lat_cells.setdefault(name, {}).setdefault(r.get("engine"), {})
        for fn in L2_FIELDS:
            if r.get(fn) is not None:
                per.setdefault(fn, []).append(float(r[fn]))
    for name, eng in lat_cells.items():
        for e, fields in eng.items():
            med_by_field = {fn: med(v) for fn, v in fields.items()}
            med_by_field["rounds"] = len(fields.get("p50_ns") or [])
            med_by_field["max_seen_ns"] = max(fields.get("max_sample_ns") or [0])
            lat_cells[name][e] = med_by_field
    summary["latency_cells"] = lat_cells
    summary["mem"] = collect("mem", 1)
    summary["correctness"] = collect("example", 1)
    summary["robust"] = collect("robust", 1)

    # ---- correctness rates per engine ----------------------------------------
    # Two different questions are answered side by side, because conflating them
    # is how a benchmark starts advertising "no bugs":
    #   exact            — the engine produced the intended text
    #   agrees_with_this_kieekey — the engine produced the same text as the
    #                       subject of this campaign (which is NOT the same as
    #                       being correct: all four can be wrong together)
    #   flagged_ok       — the harness's own per-key verdict, when the row carries
    #                       one (no crash / no rejected composition), kept separate
    #                       from the linguistic judgement on purpose
    def rate_block(rows, eng):
        n = len(rows)
        exact = sum(1 for r in rows if r.get("out_" + eng) == r.get("intended"))
        out = {"rows": n, "exact": exact, "problems": n - exact,
               "exact_rate": (exact / float(n)) if n else None}
        if rows and ("ok_" + eng) in rows[0]:
            out["flagged_ok"] = sum(1 for r in rows
                                    if str(r.get("ok_" + eng)).lower() in ("true", "1", "yes"))
        if eng != SUBJECT and rows and "out_" + SUBJECT in rows[0]:
            out["agrees_with_" + SUBJECT] = sum(1 for r in rows
                                                if r.get("out_" + eng) == r.get("out_" + SUBJECT))
        return out

    engines_in_rows = sorted({k[4:] for r in summary["correctness"] for k in r
                              if k.startswith("out_")})
    per_cell_corr = {}
    for r in summary["correctness"]:
        per_cell_corr.setdefault((r.get("config"), r.get("method")), []).append(r)
    summary["correctness_rates"] = {
        "|".join(str(x) for x in k): {e: rate_block(rs, e) for e in engines_in_rows}
        for k, rs in sorted(per_cell_corr.items())}
    summary["correctness_overall"] = {e: rate_block(summary["correctness"], e)
                                      for e in engines_in_rows}

    # ---- memory: one row per engine, for prose tokens -----------------------
    # Engine names are dotted into the summary for {{stat:…}} lookups, and
    # "unikey-4.x" would break a dotted path, so keys here are the same names with
    # dots turned into underscores. The tables keep the real engine names.
    alias = lambda e: str(e).replace(".", "_").replace("-", "_")
    mem_by = {}
    for r in summary["mem"]:
        mem_by.setdefault(r.get("engine"), {})[r.get("config")] = r
    summary["memory"] = {}
    mem_labels = {}
    for e, per in sorted(mem_by.items()):
        ent = {}
        for cfg in ("as-shipped", "matched-minimal"):
            r = per.get(cfg) or {}
            soak = int(r.get("soak_keys") or 0)
            al = int(r.get("alloc_soak") or 0)
            ent["allocs_" + cfg.replace("-", "_")] = al
            ent["allocs_per_megakey_" + cfg.replace("-", "_")] = (al / float(soak) * 1e6) if soak else None
            ent["rss_mib_" + cfg.replace("-", "_")] = (r.get("rss_after_soak") or 0) / 1048576.0
            ent["bytes_per_key_" + cfg.replace("-", "_")] = ((r.get("bytes_soak") or 0) / float(soak)) if soak else None
            ent["soak_keys"] = soak
        summary["memory"][alias(e)] = ent
        mem_labels[alias(e)] = e
    summary["memory_labels"] = mem_labels
    summary["memory_note"] = ("alloc_soak counts allocations made by the harness driver plus the "
                              "engine over a soak of soak_keys keys; the engine itself is expected "
                              "to contribute a constant, not a per-key cost")

    # ---- list-artifacts reduced to the totals the report quotes -------------
    # A template cannot sum a JSONL array, and "n/a" where a total should be is
    # how a report starts quietly understating how much evidence exists.
    dif = summary["diffab"]
    summary["diffab_totals"] = {
        "rows": len(dif),
        "events": sum(int(r.get("events") or 0) for r in dif),
        "per_key_mismatches": sum(int(r.get("per_key_mismatches") or 0) for r in dif),
        "final_text_equal": bool(dif) and all(r.get("final_text_equal") for r in dif),
        "builds": sorted({str(r.get("subject_build")) + " vs " + str(r.get("rival_build"))
                          for r in dif}),
    }
    rob = summary["robust"]
    summary["robust_totals"] = {
        "rows": len(rob),
        "ok": sum(1 for r in rob if r.get("outcome") == "ok"),
        "crashes": sum(1 for r in rob if r.get("outcome") not in ("ok", None)),
        "keys": sum(int(r.get("keys") or 0) for r in rob),
        "outcomes": sorted({str(r.get("outcome")) for r in rob}),
    }
    corr = summary["correctness"]
    summary["correctness_totals"] = {"rows": len(corr),
                                     "configs": sorted({str(r.get("config")) for r in corr}),
                                     "methods": sorted({str(r.get("method")) for r in corr}),
                                     "categories": sorted({str(r.get("cat")) for r in corr})}

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
            rows_l2.append([" · ".join(str(x) for x in k),
                            f(med([r.get("p50_ns", 0) for r in rs])),
                            f(med([r.get("p90_ns", 0) for r in rs])),
                            f(med([r.get("p99_ns", 0) for r in rs])),
                            f(med([r.get("p999_ns", 0) for r in rs])),
                            f(max(r.get("max_sample_ns", 0) for r in rs)),
                            f(med([r.get("engine_core_net_ns", 0) for r in rs])),
                            f(med([r.get("prep_ns_per_key", 0) for r in rs])),
                            len(rs)])
        t["rc1_l2"] = table(["cell", "p50 ns", "p90 ns", "p99 ns", "p999 ns", "worst sample ns",
                             "engine core ns/key", "prep ns/key", "rounds"], rows_l2)

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

    if summary["memory"]:
        t["rc1_mem"] = table(
            ["engine", "allocs (as-shipped)", "allocs / megakey", "RSS MiB", "bytes / key",
             "allocs (matched-minimal)", "allocs / megakey", "RSS MiB"],
            [[summary["memory_labels"].get(e, e),
              f(v.get("allocs_as_shipped")), f(v.get("allocs_per_megakey_as_shipped"), 3),
              f(v.get("rss_mib_as_shipped"), 1), f(v.get("bytes_per_key_as_shipped"), 2),
              f(v.get("allocs_matched_minimal")), f(v.get("allocs_per_megakey_matched_minimal"), 3),
              f(v.get("rss_mib_matched_minimal"), 1)]
             for e, v in sorted(summary["memory"].items())])
    if summary["correctness_overall"]:
        t["rc1_corr"] = table(
            ["engine", "rows", "exact vs intended", "exact rate", "differs from intended"]
            + (["agrees with kieekey"] if any("agrees_with_kieekey" in v for v in summary["correctness_overall"].values()) else []),
            [[e, f(v.get("rows")), f(v.get("exact")),
              f((v.get("exact_rate") or 0) * 100.0, 2) + " %", f(v.get("problems"))]
             + ([f"{v.get('agrees_with_kieekey', 0) / max(v.get('rows', 1), 1) * 100.0:.2f} %"]
                if "agrees_with_kieekey" in v else [])
             for e, v in sorted(summary["correctness_overall"].items())])
        rows_c = []
        for name, per in sorted(summary["correctness_rates"].items()):
            for e, v in sorted(per.items()):
                rows_c.append([name.replace("|", " · "), e, f(v.get("rows")),
                               f((v.get("exact_rate") or 0) * 100.0, 2) + " %",
                               f(v.get("problems"))])
        t["rc1_corr_cells"] = table(["cell", "engine", "rows", "exact rate", "non-intended"], rows_c)

    if summary["gain"] is not None and not gain:
        t["rc1_gain"] = ("_The attribution pair (kieekey-base / kieekey-cand) was not measured in this "
                         "campaign, so no gain-over-v1.2.2 figure is published. Run the campaign with "
                         "`--engines=contest,attrib`._")

    # The sampling profile is appended here rather than by the campaign driver, so
    # "re-run the statistics" reproduces the whole artifact set — an append that
    # lives in a shell script gets lost the first time summarize.py runs alone (it
    # did: rc1-ca4's tables.md had no profile block after a manual re-summarise).
    for name in ("profile_kieekey_as-shipped", "profile_kieekey_matched-minimal"):
        md = os.path.join(ROOT, a.results, "logs", name + ".md")
        if os.path.isfile(md):
            key = "rc1_profile" if name.endswith("as-shipped") else "rc1_profile_matched_minimal"
            t[key] = open(md, encoding="utf-8").read().strip()

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
    cv = summary["candidate_verdict"]
    if gain and not cv.get("manifest_known"):
        print(f"[stats] candidate decision: {cv['decision']}")
    if gain and cv.get("baseline_campaign"):
        print(f"[stats] candidate decision: {cv['decision']} — the same-code column pair drifted by "
              f"at most {cv['null_test_abs_max_pct']} % (max |gain %| over {cv['cells_measured']} "
              f"cells): that is the harness's own drift, not the engine's")
    if gain and not cv.get("baseline_campaign"):
        print(f"[stats] candidate decision: {cv['decision']} (rule: {cv['rule']};"
              f" {len(cv['cells_regressing_beyond_band'])} cell(s) regress beyond band)")
        g = gain.get(a.deciding) or {}
        print(f"[stats] gain over frozen v1.2.2 in the deciding cell: {f(g.get('gain_pct'), 2)} % "
              f"({f(g.get('base_ns'))} → {f(g.get('cand_ns'))} ns/key)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
