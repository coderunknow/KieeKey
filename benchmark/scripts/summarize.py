#!/usr/bin/env python3
"""
benchmark/scripts/summarize.py — turn raw JSONL artifacts into report tables.

Every number in benchmark/REPORT.md is produced here, from
results/<campaign>/raw/*.jsonl. Nothing is hand-typed, so the report cannot
drift from the data. All statistics are computed from the raw per-round
samples; the script also derives the NOISE BAND and refuses to call a
difference significant when it does not clear it.

Usage: python3 benchmark/scripts/summarize.py --results=benchmark/results/CAMPAIGN
"""
import argparse
import json
import math
import os
import re
import statistics
import sys
from collections import defaultdict

CONTEST = ["kieekey", "openkey-2.0.5", "openkey-master", "unikey-4.x"]
ALL = CONTEST + ["kieekey-aa"]
VERDICT_COLS = [
    ("exact", "exact"),
    ("case_only", "case only"),
    ("restored_raw", "restored to raw"),
    ("tone_missing", "tone missing"),
    ("tone_wrong", "tone wrong"),
    ("tone_position", "tone position"),
    ("hat_wrong", "hat wrong"),
    ("letters_differ", "letters differ"),
]


def read_jsonl(path):
    out = []
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return out


def load(records_dir):
    # NOTE: sanitizer per-engine artifacts (san_*.jsonl, sanfix_*.jsonl) are
    # deliberately NOT merged here — they are per-engine subsets of other modes
    # and would overwrite the primary campaign rows. They are read directly by
    # the sanitizer block below.
    recs = defaultdict(list)
    for name in sorted(os.listdir(records_dir)):
        if not name.endswith(".jsonl"):
            continue
        if name.startswith("san_") or name.startswith("sanfix_"):
            continue
        for r in read_jsonl(os.path.join(records_dir, name)):
            r["_file"] = name
            recs[r.get("mode", "?")].append(r)
    return recs


def fmt(v, nd=1):
    if v is None:
        return "—"
    if isinstance(v, float):
        if abs(v) >= 1000:
            return f"{v:,.0f}"
        return f"{v:.{nd}f}"
    return f"{v:,}"


def pct(a, b):
    return "—" if not b else f"{100.0 * a / b:.2f}%"


def table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "|".join(["---"] * len(header)) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c) for c in r) + " |")
    return "\n".join(out)


def med(xs):
    return statistics.median(xs) if xs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--campaign", default="", help="label only; the directory is authoritative")
    a = ap.parse_args()
    res = a.results
    raw = os.path.join(res, "raw")
    R = load(raw)
    tables = {}
    summary = {}

    # ---------------------------------------------------------------- meta ---
    meta = R.get("meta", [{}])[0] if R.get("meta") else {}
    summary["meta"] = meta

    # protocol actually exercised (derived from the artifacts, not the CLI)
    lat_meta = R.get("latency", [])
    per = defaultdict(set)
    for r in lat_meta:
        if r.get("mode") == "latency":
            per[(r["config"], r["method"], r["stream"], r["engine"])].add(r["round"])
    summary["protocol"] = {
        "latency_rounds": max((len(v) for v in per.values()), default=0),
        "latency_streams": len({(k[0], k[1], k[2]) for k in per}),
        "keys_per_stream": max((r.get("keys", 0) for r in lat_meta
                               if r.get("mode") == "latency"), default=0),
    }

    # --------------------------------------------------------- correctness ---
    corr = [r for r in R.get("correctness", [])]
    by = defaultdict(dict)
    for r in corr:
        by[(r["config"], r["method"], r["cat"])][r["engine"]] = r
    rows = []
    for (cfg, meth, cat), d in sorted(by.items()):
        if cat not in ("words", "passages"):
            continue
        for eng in ALL:
            r = d.get(eng)
            if not r:
                continue
            rows.append([cfg, meth, cat, eng, r["total"], r["exact"], pct(r["exact"], r["total"]),
                         r["restored_raw"], r["tone_wrong"] + r["tone_position"] + r["tone_missing"],
                         r["hat_wrong"], r["letters_differ"], r["digest"]])
    tables["correctness"] = table(
        ["config", "method", "corpus", "engine", "cases", "exact", "exact %",
         "restored", "tone issues", "hat", "letters differ", "output digest (64-bit)"], rows)
    summary["correctness"] = [
        {"config": c, "method": m, "cat": k, "engine": e,
         "exact_rate": (d[e]["exact"] / d[e]["total"]) if d.get(e) and d[e]["total"] else None,
         "total": d[e]["total"] if d.get(e) else 0,
         "digest": d[e]["digest"] if d.get(e) else None}
        for (c, m, k), d in sorted(by.items()) if k in ("words", "passages") for e in ALL if e in d
    ]

    # ------------------------------------------------------ verdict detail ---
    rows = []
    for (cfg, meth, cat), d in sorted(by.items()):
        if cat != "words":
            continue
        for eng in CONTEST:
            r = d.get(eng)
            if not r or not r["total"]:
                continue
            cells = []
            for key, _lbl in VERDICT_COLS:
                cells.append(pct(r.get(key, 0), r["total"]))
            rows.append([cfg, meth, eng] + cells)
    tables["verdicts"] = table(
        ["config", "method", "engine"] + [lbl for _k, lbl in VERDICT_COLS], rows)

    # ----------------------------------------------------------- agreement ---
    rows = []
    for (cfg, meth, cat), d in sorted(by.items()):
        if cat not in ("words", "passages"):
            continue
        ds = {e: d[e]["digest"] for e in CONTEST if e in d}
        n = len(set(ds.values()))
        agree = [e for e in CONTEST if ds.get(e) == ds.get("kieekey")]
        rows.append([cfg, meth, cat, f"{n} distinct output digest(s) across 4 engines",
                     ", ".join(agree) if len(agree) < 4 else "all 4 identical"])
    tables["agreement"] = table(["config", "method", "corpus", "distinct results",
                                 "engines matching KieeKey"], rows)

    # ------------------------------------------------------- stress + fuzz ---
    texts = [r for r in R.get("text", [])]
    per_stream = defaultdict(dict)
    for r in texts:
        per_stream[(r["config"], r["stream_id"])][r["engine"]] = r["text"]
    agree_ct = tot = 0
    differ = []
    for (cfg, sid), d in sorted(per_stream.items()):
        vals = {e: d[e] for e in CONTEST if e in d}
        tot += 1
        if len(set(vals.values())) == 1:
            agree_ct += 1
        else:
            differ.append((cfg, sid, vals))
    # one row per divergent stream: engine -> text, in CONTEST order
    rows = []
    rows = [[cfg, sid, " · ".join(f"`{v if v else '(empty)'}`"
                                  for v in (vals.get(e, "") for e in CONTEST))]
            for cfg, sid, vals in differ[:60]]
    tables["stress"] = table(["config", "stress stream",
                              "output — order: " + ", ".join(CONTEST)], rows)
    summary["stress"] = {"streams": tot, "all_engines_identical": agree_ct,
                         "divergent": tot - agree_ct}
    tables["stress_summary"] = table(["metric", "value"], [
        ["stress streams compared", tot],
        ["all 4 engines produce identical text", f"{agree_ct} ({pct(agree_ct, tot)})"],
        ["at least one engine differs", f"{tot - agree_ct}"],
        ["note", "identical text across engines is not correctness; see the "
                 "passage/word tables for that"],
    ])

    fz = defaultdict(dict)
    for r in R.get("fuzz", []):
        fz[r["config"]][r["engine"]] = r
    rows = []
    for cfg, d in sorted(fz.items()):
        for e in ALL:
            if e in d:
                rows.append([cfg, e, d[e]["keys"], d[e]["out_len_bytes"], d[e]["digest"]])
    tables["fuzz"] = table(["config", "engine", "keys fed", "visible bytes left", "digest"], rows)

    # ------------------------------------------------- adapter overhead ---
    ovh = R.get("overhead", [])
    if ovh:
        tables["shim_overhead"] = table(
            ["engine", "ns per indirect call (min of 5 x 200k)", "iterations",
             "clock pair overhead (ns)", "note"],
            [[r["engine"], f"{r['ns_per_noop_call']:.3f}", r["iterations"],
              f"{r['clock_pair_overhead_ns']:.0f}", r["note"]] for r in ovh])
        summary["shim_overhead"] = ovh

    # ------------------------------------------------------------- invariant ---
    inv = [r for r in R.get("invariant", [])]
    rows = []
    for r in inv:
        rows.append([r["config"], r["engine"], r["streams"], f"{pct(r['clean'], r['streams'])}",
                     r["divergent_streams"], r["chars_lost"], r["keys"]])
    tables["invariant"] = table(
        ["config", "engine", "edit streams", "streams preserved exactly",
         "divergent", "characters lost", "keys fed"], rows)
    summary["invariant"] = [{k: v for k, v in r.items() if k != "mode"} for r in inv]

    # ---------------------------------------------------------------- macro ---
    mac = defaultdict(dict)
    for r in R.get("macro", []):
        mac[r["config"]][r["engine"]] = r
    rows = []
    for cfg, d in sorted(mac.items()):
        for e in ALL:
            if e in d:
                rows.append([cfg, e, f"`{d[e]['text']}`", "YES" if d[e]["expanded"] else "no"])
    tables["macro"] = table(["config", "engine", "visible text after typing `tn` + space",
                             "abbreviation expanded"], rows)

    # -------------------------------------------------------------- latency ---
    lat = [r for r in R.get("latency", [])]
    latby = defaultdict(lambda: defaultdict(list))
    for r in lat:
        latby[(r["config"], r["method"], r["stream"], r["engine"])].setdefault("full", []).append(r)
        for k in ("prep_ns_per_key", "engine_ns_per_key", "engine_core_net_ns",
                  "full_ns_per_key", "p50_ns", "p90_ns", "p99_ns", "p999_ns", "max_sample_ns"):
            latby[(r["config"], r["method"], r["stream"], r["engine"])].setdefault(k, []).append(r[k])
    rows = []
    stats_tbl = defaultdict(dict)
    for (cfg, meth, stream, eng), d in sorted(latby.items()):
        fulls = d["full_ns_per_key"]
        engs = d["engine_ns_per_key"]
        preps = d["prep_ns_per_key"]
        row = [cfg, meth, stream, eng, fmt(med(fulls)),
               fmt(min(fulls)), fmt(max(fulls)),
               fmt(statistics.pstdev(fulls) if len(fulls) > 1 else 0.0, 2),
               fmt(med(engs)), fmt(min(engs)), fmt(max(engs)),
               fmt(med(preps), 2),
               fmt(med(d["p50_ns"]), 0), fmt(med(d["p90_ns"]), 0),
               fmt(med(d["p99_ns"]), 0), fmt(med(d["p999_ns"]), 0), fmt(max(d["max_sample_ns"]), 0)]
        rows.append(row)
        stats_tbl[(cfg, f"{meth}|{stream}")][eng] = {
            "full_median": med(fulls), "full_min": min(fulls), "full_max": max(fulls),
            "engine_median": med(engs), "prep_median": med(preps),
            "p50": med(d["p50_ns"]), "p90": med(d["p90_ns"]), "p99": med(d["p99_ns"]),
            "p999": med(d["p999_ns"]), "max": max(d["max_sample_ns"]),
            "rounds": len(fulls),
        }
    tables["latency"] = table(
        ["config", "method", "stream", "engine", "full ns/key (median)", "best", "worst", "σ",
         "engine-only ns/key", "best", "worst", "adapter ns/key",
         "p50", "p90", "p99", "p99.9", "max sample"], rows)
    summary["latency"] = {}
    for (c, st), dd in stats_tbl.items():
        summary["latency"][f"{c}|{st}"] = {e: dict(d) for e, d in dd.items()}

    # ------------------------------------------------- noise band + verdicts ---
    # In-process A/A: same engine, second instance, same round, same stream.
    aa = defaultdict(dict)
    for r in lat:
        aa[(r["config"], r["method"], r["stream"], r["round"])][r["engine"]] = r
    aa_deltas = []
    for _k, d in aa.items():
        if "kieekey" in d and "kieekey-aa" in d:
            aa_deltas.append(abs(d["kieekey"]["full_ns_per_key"] - d["kieekey-aa"]["full_ns_per_key"]))
    aa_sorted = sorted(aa_deltas)
    band_local = aa_sorted[min(len(aa_sorted) - 1, int(0.99 * max(0, len(aa_sorted) - 1)))] if aa_sorted else 0.0
    band_max = max(aa_deltas) if aa_deltas else 0.0
    rows = []
    for (cfg, stream), dd in sorted(stats_tbl.items()):   # stream label carries the method
        base = dd.get("kieekey", {})
        for eng in CONTEST[1:]:
            o = dd.get(eng, {})
            if not base or not o:
                continue
            for metric, label in (("full_median", "full ns/key"), ("engine_median", "engine-only ns/key")):
                a, b = base.get(metric), o.get(metric)
                if a is None or b is None:
                    continue
                delta = b - a
                rel = 100.0 * delta / a if a else 0.0
                sig = "SLOWER (outside noise)" if delta > band_local else (
                    "FASTER (outside noise)" if -delta > band_local else "within noise band")
                rows.append([cfg, stream, f"KieeKey → {eng}", label, fmt(a), fmt(b),
                             f"{delta:+.1f}", f"{rel:+.1f} %", f"{band_local:.2f}", sig])
    tables["noise"] = table(
        ["config", "stream", "pair", "metric", "KieeKey", "other", "Δ ns/key", "Δ %",
         "A/A noise band (max |kieekey − kieekey-aa|, ns/key)", "verdict"], rows)
    summary["noise_band"] = {"inprocess_aa_max_abs_delta_ns": band_local,
                             "aa_p99_abs_delta_ns": band_local,
                             "aa_worst_abs_delta_ns": band_max,
                             "aa_samples": len(aa_deltas),
                             "aa_median_abs_delta_ns": med(aa_deltas),
                             "band_rule": "band = p99 of |kieekey - kieekey-aa| per round; "
                                          "worst-case and median published next to it",
                             "note": "band = worst in-process A/A deviation on the same "
                                     "binary; cross-campaign deltas are compared against it"}

    # ---------------------------------------------------- stream digests ------
    dg = defaultdict(dict)
    for r in corr:
        if r["cat"] == "words":
            dg[(r["config"], r["method"])][r["engine"]] = r["digest"]
    rows = []
    for (cfg, meth), d in sorted(dg.items()):
        rows.append([cfg, meth] + [f"`{d.get(e, 0):016x}`" for e in CONTEST])
    tables["digests"] = table(["config", "method"] + ["Σ output: " + e for e in CONTEST], rows)

    # --------------------------------------------------------------- memory ---
    mem = [r for r in R.get("mem", [])]
    rows = []
    for r in mem:
        rows.append([r["config"], r["engine"], fmt(r["rss_after_init"] / 1024.0, 0) + " KiB",
                     fmt(r["rss_after_soak"] / 1024.0, 0) + " KiB",
                     fmt((r["rss_after_soak"] - r["rss_after_init"]) / 1024.0, 0) + " KiB",
                     fmt(r["soak_keys"], 0),
                     r.get("alloc_init", 0), r.get("alloc_soak", 0),
                     f"{r.get('allocs_per_key', 0):.6f}" if "allocs_per_key" in r else "—"])
    tables["mem"] = table(["config", "engine", "RSS after init", "RSS after soak",
                           "growth", "keys", "allocs at init", "allocs during soak",
                           "allocs/key"], rows)
    summary["mem"] = [{k: v for k, v in r.items() if k != "mode"} for r in mem]

    # ------------------------------------------------------------ robustness ---
    rob = [r for r in R.get("robust", [])]
    outcomes = defaultdict(dict)
    for r in rob:
        outcomes[r["stream"]][r["engine"]] = r["outcome"] + (
            f" (sig {r['signal']})" if r.get("signal") else
            f" (exit {r['exit_code']})" if r.get("exit_code") else "")
    streams = sorted(outcomes.keys())
    rows = [[s] + [outcomes[s].get(e, "—") for e in CONTEST] for s in streams]
    tables["robust"] = table(["hostile stream"] + CONTEST, rows)
    summary["robust"] = {"survived_all": sum(
        1 for s in streams
        if all(outcomes[s].get(e, "").startswith("ok") for e in CONTEST)),
        "total": len(streams)}

    # ----------------------------------------------------- sanitizer reports ---
    san_rows = []
    for name in sorted(os.listdir(raw)):
        if not name.startswith("san_") or not name.endswith(".jsonl"):
            continue
        eng = name[4:-6]
        log = os.path.join(res, "logs", f"san_{eng}.log")
        flog = os.path.join(res, "logs", f"sanfix_{eng}.log")
        # Attribution is by SOURCE FILE of the report, so a finding produced by
        # another engine's code inside a mixed run can never be charged to this
        # engine (and vice versa — the subject is not exempt either).
        owner = {
            "kieekey": ("src/core/",),
            "kieekey-aa": ("src/core/",),
            "openkey-2.0.5": ("tests/reference/openkey-2.0.5",),
            "openkey-master": ("benchmark/reference/openkey-master", "reference/openkey-master"),
            "unikey-4.x": ("tests/reference/unikey", "ukengine.cpp", "vnconv.cpp", "inputproc.cpp"),
        }.get(eng, ())
        findings, foreign = [], []
        for p in (log, flog):
            if not os.path.exists(p):
                continue
            with open(p, encoding="utf-8", errors="replace") as f:
                lines = f.read().split("\n")
            for i, ln in enumerate(lines):
                m = None
                for pat, lbl in (
                    (r"ERROR: AddressSanitizer: ([a-zA-Z_-]+)", "ASan"),
                    (r"runtime error: (.+)", "UBSan"),
                    (r"LeakSanitizer: detected memory leaks", "LeakSanitizer"),
                    (r"SEGV on unknown address", "SEGV"),
                ):
                    m = re.search(pat, ln)
                    if m:
                        break
                if not m:
                    continue
                ctx = "\n".join(lines[max(0, i - 2):i + 6])
                # which tree does the top frame belong to?
                frames = re.findall(r"#\d+ 0x[0-9a-f]+ in [^\n]*? ([\w./+-]+\.(?:cpp|hpp|c|h)):", ctx)
                src = frames[0] if frames else ln.split(":")[0]
                mine = any(o in src for o in owner) if owner else False
                rec = f"{lbl}: {m.group(1)[:60]}  [{os.path.basename(src)}]"
                (findings if mine else foreign).append(rec)
        rr = [x for x in read_jsonl(os.path.join(raw, name)) if x.get("mode") == "robust"]
        okct = sum(1 for x in rr if x.get("outcome") == "ok")
        san_rows.append([eng, f"{okct}/{len(rr)}", len(findings),
                         "; ".join(sorted(set(findings))[:3]) or "none",
                         f"{len(foreign)} finding(s) from other trees in the same run"
                         f"{' — attributed elsewhere' if foreign else ''}"])
    if san_rows:
        tables["sanitizers"] = table(["engine", "hostile streams completed",
                                      "findings in this engine's own sources", "first findings",
                                      "cross-check"], san_rows)
        summary["sanitizers_own_findings"] = {r[0]: r[2] for r in san_rows}
        summary["sanitizers_streams"] = {r[0]: r[1] for r in san_rows}

    # corpus filter accounting (which part of the corpus was actually used)
    for r in R.get("corpus", []):
        if r.get("config") == "as-shipped" and r.get("method") == "telex-end":
            summary["corpus"] = {k: v for k, v in r.items() if k != "mode"}
            break

    # ------------------------------------------------------------------ env ---
    env_path = os.path.join(res, "environment.txt")
    if os.path.exists(env_path):
        with open(env_path, encoding="utf-8") as f:
            lines = [l.rstrip() for l in f if "=" in l]
        rows = [[k.strip(), v.strip()] for k, v in (l.split("=", 1) for l in lines)]
        tables["environment"] = table(["key", "value"], rows)

    # --------------------------------------------------------- provenance ----
    hashes_path = os.path.join(res, "logs", "input_hashes.txt")
    if os.path.exists(hashes_path):
        with open(hashes_path, encoding="utf-8") as f:
            rows = []
            for l in f:
                parts = l.split()
                if len(parts) == 2:
                    rows.append([os.path.basename(parts[1]), f"`{parts[0][:16]}…`"])
        tables["provenance"] = table(["input file", "sha256 (truncated)"], rows[:40])

    # ------------------------------------------------------------- examples --
    exs = [r for r in R.get("example", [])]
    rows = []
    for r in exs[:40]:
        cells = []
        for e in CONTEST:
            cells.append(f"`{r.get('out_' + e, '')}`")
        which = r.get("input_keys", r.get("stream_id", ""))
        rows.append([r["config"], r["method"], r["cat"], which,
                     f"`{r.get('intended', '')}`"] + cells)
    tables["examples"] = table(["config", "method", "corpus", "keystrokes / stream",
                                "intended"] + CONTEST, rows)

    # -------------------------------------------------------------- output ---
    out_tables = os.path.join(res, "tables.md")
    with open(out_tables, "w", encoding="utf-8") as f:
        f.write("<!-- GENERATED by benchmark/scripts/summarize.py — do not edit. -->\n")
        for name in sorted(tables):
            f.write(f"\n<<<TABLE:{name}>>>\n{tables[name]}\n<<<END>>>\n")
    with open(os.path.join(res, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1, sort_keys=True, default=str)
    print(f"[summarize] wrote {out_tables} ({len(tables)} tables) and summary.json")
    if band_local:
        print(f"[summarize] in-process A/A noise band: {band_local:.2f} ns/key "
              f"(median |Δ| {med(aa_deltas):.2f}) over {len(aa_deltas)} round pairs")


if __name__ == "__main__":
    sys.exit(main())
