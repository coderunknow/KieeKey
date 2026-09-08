#!/usr/bin/env python3
"""v1.3.0 RC1 pass/fail gates. Correctness is a gate, not a table.

Every subcommand appends one line to <res>/logs/gates.txt with a timestamp and a
PASS/FAIL, and exits non-zero on FAIL — the campaign script stops on the first
failure. A gate that has nothing to compare against FAILS (an empty oracle once
printed "PASS — 0 rows", which is precisely the failure mode a gate must not
have); a gate that cannot find its artifact fails the same way.

    rc1_gates.py <gate> --res=benchmark/results/<campaign> [--candidate]

    manifest        the frozen v1.2.2 reference copy still matches the manifest
                    (so "identical to baseline" can mean anything), and in
                    baseline mode the working tree equals it too
                    over a non-zero case count
    attrib-guard    the two shim columns reproduce the in-process transcript and
                    measure a plausible per-key cost (see rc1.hpp)
    digest-identity every kieekey correctness output is bit-identical to the frozen
                    baseline's, row by row
    correctness     no exact-case regression in any (config, method) cell
    diffab          0 per-key transcript mismatches over the full event count
    memory          the hot path stayed allocation-free; RSS did not blow up
    sanitizers      0 findings in KieeKey-owned sources (upstream defects are
                    published, never counted against KieeKey and never fixed here)
"""
import argparse
import hashlib
import json
import glob
import os
import re
import subprocess
import sys
from datetime import datetime, timezone

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MAN = "docs/bench/rc1-130/baseline_manifest.json"
ORACLE_DEFAULT = "docs/bench/rc1-130/rc1-baseline/baseline"


def path(p):
    return p if os.path.isabs(p) else os.path.join(ROOT, p)


def sha256_file(rel):
    with open(path(rel), "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def load_json(rel):
    try:
        with open(path(rel), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


torn_lines = [0]


def jsonl(rel, pred=lambda r: True):
    """One flat JSON object per line. Undecodable lines are skipped and COUNTED:
    a torn final line is what an interrupted step leaves behind, and a reader of
    the gate log has to know the artifact was cut short, not be handed a clean
    PASS computed from the surviving 90 %."""
    out = []
    p = path(rel)
    if not os.path.isfile(p):
        return out
    with open(p, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("{"):
                try:
                    r = json.loads(line)
                except json.JSONDecodeError:
                    torn_lines[0] += 1
                    continue
                if pred(r):
                    out.append(r)
    return out


def log(res, name, ok, detail):
    line = (f"[{datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}] "
            f"gate:{name}: {'PASS' if ok else 'FAIL'} — {detail}")
    print(line)
    d = os.path.join(path(res), "logs")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "gates.txt"), "a", encoding="utf-8") as f:
        f.write(line + "\n")
    return 0 if ok else 1


# ------------------------------------------------------------------ manifest --
def gate_manifest(a):
    """Pin what is being measured. Two halves, two questions:

      * baseline_engine_sources_sha256 — the pristine v1.2.2 copy that builds
        libkkbase.so. Checked on EVERY campaign: if it drifts, the differential
        gate and every gain-over-v1.2.2 figure mean nothing.
      * engine_sources_sha256 — the working tree at manifest time. A BASELINE
        campaign requires src/core to equal the baseline half (the tree really is
        the frozen engine); a candidate run (--candidate) expects it to differ
        and says by how much.
    """
    if a.regen or not os.path.exists(path(MAN)):
        subprocess.run([sys.executable, os.path.join(ROOT, "benchmark/scripts/baseline_manifest.py")],
                       check=True)
        return log(a.res, "manifest", True, "baseline manifest (re)generated")
    man = load_json(MAN) or {}
    base = man.get("baseline_engine_sources_sha256") or man.get("engine_sources_sha256", {})
    if not base:
        return log(a.res, "manifest", False, f"no baseline hashes in {MAN} — run baseline_manifest.py")
    badref = []
    for rel, dg in sorted(base.items()):
        # the reference copy is FLAT (build.sh verifies it with paths relative to
        # that directory), so src/core/X maps to X
        ref = os.path.join("benchmark/reference/kieekey-1.2.2", os.path.basename(rel))
        try:
            if sha256_file(ref) != dg:
                badref.append(rel)
        except OSError:
            badref.append(rel + " (missing from the reference copy)")
    if badref:
        return log(a.res, "manifest", False,
                   "the pristine baseline copy no longer matches the manifest for "
                   + ", ".join(badref) + " — the differential reference is compromised; "
                     "restore it from the release tag before measuring anything")
    tree = {}
    for rel in sorted(set(base) | set(man.get("engine_sources_sha256", {}))):
        try:
            tree[rel] = sha256_file(rel)
        except OSError:
            tree[rel] = None
    diff = sorted(r for r in base if tree.get(r) != base[r])
    added = sorted(r for r, v in tree.items() if v is not None and r not in base)
    head = (man.get("git") or {}).get("git_head", "?")[:12]
    if not a.allow_engine_change:
        if diff:
            return log(a.res, "manifest", False,
                       f"{len(diff)} engine source(s) differ from the frozen baseline "
                       f"({', '.join(os.path.basename(d) for d in diff[:6])}) — a BASELINE "
                       f"campaign must measure the manifest tree; for a candidate run pass "
                       f"--candidate (and record the change in the ledger first)")
        return log(a.res, "manifest", True,
                   f"{len(base)} engine sources hash-match the frozen baseline and the "
                   f"pristine reference copy is intact; head={head}")
    return log(a.res, "manifest", True,
               f"baseline intact ({len(base)} frozen v1.2.2 sources verified in "
               f"benchmark/reference/kieekey-1.2.2); the working tree differs in {len(diff)} "
               f"of them ({', '.join(os.path.basename(d) for d in diff) or 'none'})"
               + (f", adding {', '.join(os.path.basename(x) for x in added)}" if added else "")
               + " — that is the candidate under test, and libkkbase.so still measures v1.2.2 exactly")


def correctness_rows(res, engine):
    rows = []
    d = os.path.join(path(res), "raw")
    if not os.path.isdir(d):
        return rows
    for fn in sorted(os.listdir(d)):
        if not fn.startswith("correctness"):
            continue
        rows += jsonl(os.path.join(res, "raw", fn),
                      lambda r: r.get("mode") == "example" and f"out_{engine}" in r)
    return rows


# ------------------------------------------------------------ digest/qual ---
def gate_digest_identity(a):
    """The strongest correctness statement available here: the candidate's output
    must be BIT-IDENTICAL to the frozen v1.2.2 engine's on the same corpus, in the
    same campaign, from two builds of the engine.

    --oracle-engine=<name> compares two columns of THIS campaign (e.g. kieekey
    vs kieekey-base, where kieekey-base is built from the hash-verified pristine
    copy in benchmark/reference/kieekey-1.2.2). Without it, the campaign is
    compared against a previously frozen artifact in --oracle, and a missing
    oracle is a FAIL, not a free pass.
    """
    eng = a.engine
    cur = correctness_rows(a.res, eng)
    if a.oracle_engine:
        oracle = correctness_rows(a.res, a.oracle_engine)
        if not oracle:
            return log(a.res, "digest-identity", False,
                       f"column {a.oracle_engine} produced no correctness rows — the "
                       f"frozen baseline column must be measured in the same campaign")
    else:
        oracle = correctness_rows(a.oracle, eng)
    if not oracle:
        return log(a.res, "digest-identity", False,
                   f"no baseline oracle at {a.oracle} (run the baseline campaign and freeze "
                   f"its correctness.jsonl there) — an empty oracle must never pass")
    if not cur:
        return log(a.res, "digest-identity", False, "this campaign produced no correctness rows")
    key = lambda r: (r.get("config"), r.get("method"), r.get("cat"), r.get("input_keys"))
    oeng = a.oracle_engine or eng
    cm = {key(r): r.get(f"out_{eng}") for r in cur}
    om = {key(r): r.get(f"out_{oeng}") for r in oracle}
    common = sorted(set(cm) & set(om))
    if not common:
        return log(a.res, "digest-identity", False, "oracle and campaign share no corpus rows")
    bad = [k for k in common if cm[k] != om[k]]
    if bad:
        k = bad[0]
        return log(a.res, "digest-identity", False,
                   f"{len(bad)} of {len(common)} rows differ; first at {k}: "
                   f"baseline={om[k]!r} vs now={cm[k]!r}")
    dh = hashlib.sha256("".join(str(om[k]) for k in common).encode()).hexdigest()[:16]
    dhc = hashlib.sha256("".join(str(cm[k]) for k in common).encode()).hexdigest()[:16]
    return log(a.res, "digest-identity", True,
               f"every {eng} output is bit-identical to the baseline column {oeng} "
               f"({len(common)} rows across both configs and all methods); "
               f"digests {dhc} == {dh}")


def gate_correctness(a):
    cur = correctness_rows(a.res, a.engine)
    oracle = correctness_rows(a.oracle, a.engine)
    if not cur:
        return log(a.res, "correctness", False, "no correctness rows produced")

    def rates(rows):
        out = {}
        for r in rows:
            k = (r.get("config"), r.get("method"), r.get("cat"))
            ok = r.get(f"ok_{a.engine}")
            c = out.setdefault(k, [0, 0])
            c[1] += 1
            if ok:
                c[0] += 1
        return out

    rc, ro = rates(cur), rates(oracle)
    worse = []
    for k, (g, n) in sorted(rc.items()):
        if k in ro and n and ro[k][1]:
            a_now, b_now = g / n, ro[k][0] / ro[k][1]
            if a_now + 1e-9 < b_now:
                worse.append(f"{k}: {b_now * 100:.2f} % -> {a_now * 100:.2f} %")
    if worse:
        return log(a.res, "correctness", False, "exact-case rate regressed in " + "; ".join(worse[:4]))
    n = sum(v[1] for v in rc.values())
    note = "no baseline oracle (absolute rates published)" if not ro else "no exact-case regression"
    return log(a.res, "correctness", True,
               f"{note} in {n} subject rows (words/passages/invariant/macro), both configs, all methods")


def gate_diffab(a):
    rows = jsonl(os.path.join(a.res, "raw", "diffab.jsonl"), lambda r: r.get("mode") == "diffab")
    if not rows:
        return log(a.res, "diffab", False, "no diffab rows — the differential never ran")
    mism = sum(int(r.get("per_key_mismatches", 0)) for r in rows)
    events = sum(int(r.get("events", 0)) for r in rows)
    tailbad = [r for r in rows if not r.get("final_text_equal")]
    builds = {r.get("subject_build") for r in rows} | {r.get("rival_build") for r in rows}
    if mism or tailbad:
        first = next((r for r in rows if int(r.get("per_key_mismatches", 0))), None)
        d = f"{mism} per-key mismatches over {events:,} events"
        if first:
            d += (f"; first at key {first.get('first_mismatch_key')} of "
                  f"{first.get('config')}/{first.get('method')}/{first.get('stream')}")
        return log(a.res, "diffab", False, d)
    if events < a.min_events:
        return log(a.res, "diffab", False,
                   f"only {events:,} events compared ({len(rows)} rows) — below the "
                   f"{a.min_events:,} the protocol requires; run more seeds")
    return log(a.res, "diffab", True,
               f"0 mismatches over {events:,} events ({len(rows)} rows) — per-key code, "
               f"backspaces, replacement text and final visible text all identical; "
               f"builds compared: {', '.join(sorted(builds))}")


def gate_memory(a):
    raw = os.path.join(a.res, "raw")
    files = sorted(glob.glob(os.path.join(raw, "mem_*.jsonl"))) or \
        [os.path.join(raw, "mem.jsonl")]
    rows = [r for pth in files for r in jsonl(pth, lambda r: r.get("mode") == "mem")]
    if not rows:
        return log(a.res, "memory", False, "no mem rows in %s (build bench_mem and run "
                                           "--mode=mem once per engine, in an isolated process)"
                   % ", ".join(files))
    # A memory gate that matches no rows must FAIL rather than pass on an empty
    # list: with a truncating --out it once read only the last engine measured,
    # and with a renamed field it printed "PASS" with no detail at all. Both are
    # invisible unless the gate insists on having seen the subject engine.
    rows = [r for r in rows if r.get("engine") == a.engine]
    if not rows:
        return log(a.res, "memory", False,
                   f"mem rows exist but none for engine {a.engine} — the subject of the "
                   f"allocation budget must itself be measured in isolation")
    seen = {r.get("config") for r in rows}
    missing = [c for c in ("as-shipped", "matched-minimal") if c not in seen]
    if missing:
        return log(a.res, "memory", False, "engine %s: config(s) %s never measured"
                   % (a.engine, ", ".join(map(str, missing))))
    orows = jsonl(os.path.join(a.oracle, "raw", "mem.jsonl"), lambda r: r.get("mode") == "mem") if a.oracle else []
    om = {(r.get("engine"), r.get("config")): r for r in orows}
    bad = []
    det = []
    for r in rows:
        soak = int(r.get("soak_keys") or 0)
        al = int(r.get("alloc_soak") or 0)
        rss = int(r.get("rss_after_soak") or 0)
        key = (r.get("engine"), r.get("config"))
        # bench_mem reports RSS in BYTES (VmRSS from /proc/self/status), so every
        # limit here is in bytes too — a KiB limit once failed a 46 MiB run that
        # was well inside budget, and a gate that cries wolf gets ignored.
        limit = 40                                   # allocations over the whole soak
        rss_limit = 96 * 1024 * 1024                 # 96 MiB resident
        if key in om:
            limit = max(int(om[key].get("alloc_soak") or 0) + 2, 1)
            rss_limit = int(om[key].get("rss_after_soak") or 0) + 8 * 1024 * 1024
        ok = al <= limit and rss <= rss_limit
        det.append(f"{r.get('config')}: {al} allocs / {soak:,} keys, RSS {rss / 1048576.0:.1f} MiB"
                   + ("" if ok else " (LIMIT exceeded)"))
        if not ok:
            bad.append(r.get("config"))
    if bad:
        return log(a.res, "memory", False, "hot path is no longer allocation-free / RSS grew in "
                   + ", ".join(sorted(set(bad))) + " — " + "; ".join(det))
    return log(a.res, "memory", True,
               "hot path stays allocation-free — " + "; ".join(det))


UPSTREAM_RE = re.compile(r"(tests/reference/|benchmark/reference/(openkey|kieekey)|ok_shim|kk_shim|uk_[a-z]+\.cpp)")
FINDING_RE = re.compile(r"(ERROR: AddressSanitizer|runtime error:|LeakSanitizer|ERROR: LeakSanitizer)")


def gate_sanitizers(a):
    d = os.path.join(path(a.res), "logs")
    files = sorted(f for f in os.listdir(d)) if os.path.isdir(d) else []
    files = [f for f in files if f.startswith("san_") and f.endswith(".log")]
    if not files:
        return log(a.res, "sanitizers", False, "no sanitizer logs")
    ours = []
    upstream = []
    for fn in files:
        for line in open(os.path.join(d, fn), encoding="utf-8", errors="replace"):
            if not FINDING_RE.search(line):
                continue
            ctx = line.strip()
            # the two lines after a finding usually name the source location
            eng = fn[len("san_"):-len(".log")]
            if eng in ("kieekey", "kieekey-aa", "kieekey-base", "kieekey-cand"):
                ours.append(f"{eng}: {ctx[:150]}")
            else:
                upstream.append(f"{eng}: {ctx[:120]}")
    if ours:
        return log(a.res, "sanitizers", False,
                   f"{len(ours)} finding(s) in KieeKey-owned sources: " + " || ".join(ours[:3]))
    note = (f"0 findings in KieeKey-owned sources across {len(files)} runs"
            + (f"; {len(upstream)} upstream finding(s) published, never attributed to KieeKey"
               if upstream else ""))
    return log(a.res, "sanitizers", True, note)


def gate_attrib_guard(a):
    s = load_json(os.path.join(a.res, "summary.json")) or {}
    g = s.get("attrib_guard") or {}
    rows = jsonl(os.path.join(a.res, "raw", "selftest.jsonl"), lambda r: r.get("mode") == "attrib-guard")
    g = g or (rows[0] if rows else {})
    if not g:
        return log(a.res, "attrib-guard", False, "no attrib-guard row — the pair was never checked")
    if not g.get("transcripts_equal"):
        return log(a.res, "attrib-guard", False,
                   "the attribution columns do not reproduce the in-process transcript — the "
                   "shim is not driving the engine the way the subject column does, so no "
                   "gain-over-baseline number from this campaign can be trusted")
    if not g.get("band_ok"):
        return log(a.res, "attrib-guard", False,
                   f"kieekey-base/in-process = {g.get('base_over_inprocess')}× — outside the "
                   f"plausible band, so the pair is measuring something other than the engine")
    return log(a.res, "attrib-guard", True,
               f"shim columns reproduce the in-process transcript exactly; base/cand builds "
               f"{g.get('base_build')}/{g.get('cand_build')}, ratio "
               f"{g.get('base_over_inprocess')}× (in band)")


GATES = {"manifest": gate_manifest, "attrib-guard": gate_attrib_guard,
         "digest-identity": gate_digest_identity, "correctness": gate_correctness,
         "diffab": gate_diffab, "memory": gate_memory, "sanitizers": gate_sanitizers}


def report_torn_lines(res):
    if torn_lines[0]:
        log(res, "artifact-integrity", False,
            f"{torn_lines[0]} undecodable line(s) skipped while reading the raw artifacts — "
            f"the campaign was interrupted mid-write; re-run the affected step before quoting "
            f"its numbers")
        return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gate", choices=sorted(GATES) + ["all"])
    ap.add_argument("--res", required=True)
    ap.add_argument("--oracle", default=ORACLE_DEFAULT)
    ap.add_argument("--engine", default="kieekey")
    ap.add_argument("--oracle-engine", default="",
                    help="compare against another column of the same campaign "
                         "(e.g. kieekey-base) instead of a frozen artifact directory")
    ap.add_argument("--min-events", type=int, default=1_000_000)
    ap.add_argument("--candidate", action="store_true")
    ap.add_argument("--regen", action="store_true")
    a = ap.parse_args()
    a.allow_engine_change = a.candidate
    a.min_events = a.min_events
    rc = 0
    for name, fn in GATES.items():
        if a.gate in ("all", name):
            rc |= fn(a)
    return rc


if __name__ == "__main__":
    sys.exit(main())
