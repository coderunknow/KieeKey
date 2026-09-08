#!/usr/bin/env python3
"""Assemble benchmark/REPORT.md from measured artifacts — no hand-typed numbers.

The prose lives in benchmark/REPORT.narrative.md and contains only
placeholders of the forms below; every digit in the final report is read out of
results/<campaign>/summary.json / tables.md (produced by summarize.py from the
raw JSONL artifacts), so a stale or invented figure cannot survive a rebuild.

  {{table:NAME}}                          generated markdown table (tables.md)
  {{meta:KEY}}                            campaign metadata (seed, dates, ...)
  {{env:KEY}}                             host freeze line (environment.txt)
  {{corr:cfg|method|cat|engine|field}}    one correctness figure
  {{lat:cfg|stream|engine|field}}         one latency figure (ns/key)
  {{latbest:cfg|stream|field}}            engine with the best value (+{{latbestval}})
  {{latgap:cfg|stream|field}}             KieeKey vs best, with verdict vs band
  {{corrbest:cfg|method|cat|field}}       engine with the best exact rate
  {{band}} / {{band_med}} / {{band_n}}    in-process A/A noise band
  {{paired}}                              cross-campaign verdict table
  {{verdict:cfg|stream|engine}}           significance verdict for one pair

Usage:
    python3 benchmark/scripts/make_report.py --campaign=campaign-a \
        [--ref=campaign-b]
"""
import argparse
import datetime
import json
import os
import re
import statistics
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def load_json(path):
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load_tables(path):
    txt = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"<<<TABLE:([a-z_]+)>>>\n(.*?)\n<<<END>>>", txt, re.S):
        out[m.group(1)] = m.group(2).strip("\n")
    return out


def load_env(path):
    out = {}
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            for k, v in re.findall(r"([a-zA-Z_][a-zA-Z_0-9]*)=([^=\n]*?)(?=\s[a-zA-Z_][a-zA-Z_0-9]*=|$)",
                                   line.strip()):
                out[k] = v.rstrip()
    return out


def num(v, nd=1):
    if v is None:
        return "n/a"
    if isinstance(v, float):
        if abs(v) >= 1000:
            return f"{v:,.0f}"
        return f"{v:.{nd}f}"
    return f"{v:,}"


def rate(v):
    return "n/a" if v is None else f"{100.0 * v:.2f}%"


class Report:
    def __init__(self, res, ref=None):
        self.res = res
        self.strict_stat = False
        self.s = load_json(os.path.join(res, "summary.json"))
        if self.s is None:
            sys.exit(f"missing {res}/summary.json — run scripts/summarize.py first")
        self.t = load_tables(os.path.join(res, "tables.md"))
        self.env = load_env(os.path.join(res, "environment.txt"))
        self.ref = load_json(os.path.join(ref, "summary.json")) if ref else None
        self.refTables = load_tables(os.path.join(ref, "tables.md")) if ref else {}

    # ---------------------------------------------------------------- lookups --
    # v1.3.0 RC1 artifacts. These read the summary the RC1 campaign writes
    # (cells / gain / latency_cells / verdict) instead of the v1.2.2 shapes, and
    # the gate log, so the report can quote a verdict without anyone retyping it.
    def gate(self, name):
        path = os.path.join(self.res, "logs", "gates.txt")
        if not os.path.isfile(path):
            return None
        hit = None
        for line in open(path, encoding="utf-8"):
            if f"gate:{name}:" in line:
                hit = line.strip()
        if hit is None:
            return None
        # "[2026-..Z] gate:name: PASS — detail" -> "name: PASS — detail"
        return hit.split(f"gate:{name}:", 1)[1].strip(" -")

    def gates_summary(self):
        path = os.path.join(self.res, "logs", "gates.txt")
        if not os.path.isfile(path):
            return "no gate log"
        lines = [l.strip() for l in open(path, encoding="utf-8") if ": gate:" in l]
        ok = sum(1 for l in lines if ": PASS" in l)
        bad = [l.split("gate:", 1)[1] for l in lines if ": FAIL" in l]
        out = f"{ok} of {len(lines)} gates PASS"
        if bad:
            out += "; FAILED: " + " | ".join(bad)
        return out

    @staticmethod
    def _cell(summary, block, key, *path):
        node = (summary or {}).get(block, {})
        if key not in node and "|" in key:
            key = "|".join(key.split("|"))          # keys are cfg|method|stream verbatim
        node = node.get(key)
        if node is None:
            return None
        for part in path:
            if isinstance(node, dict):
                node = node.get(part)
            else:
                return None
        return node

    def cell(self, key, engine, field):
        v = self._cell(self.s, "cells", key, engine, field)
        return num(v, 2) if isinstance(v, float) else ("n/a" if v is None else num(v))

    def dcell(self, key, field):
        v = self._cell(self.s, "cells", key, "d", field)
        return num(v, 2) if isinstance(v, float) else ("n/a" if v is None else num(v))

    def gaincell(self, key, field):
        v = self._cell(self.s, "gain", key, field)
        return num(v, 2) if isinstance(v, float) else ("n/a" if v is None else num(v))

    def l2cell(self, key, engine, field):
        v = self._cell(self.s, "latency_cells", key, engine, field)
        return num(v, 2) if isinstance(v, float) else ("n/a" if v is None else num(v))

    def regressing(self, limit=None):
        cells = ((self.s or {}).get("verdict") or {}).get("regressing_cells") or []
        if not cells:
            return "none"
        shown = cells if limit is None else cells[:limit]
        txt = " · ".join(c.replace("|", " / ") for c in shown)
        if limit is not None and len(cells) > limit:
            txt += f" (+{len(cells) - limit} more — see the cell table)"
        return txt

    def corr(self, cfg, method, cat, engine, field):
        for r in self.s.get("correctness", []):
            if (r["config"], r["method"], r["cat"], r["engine"]) == (cfg, method, cat, engine):
                v = r.get(field)
                return rate(v) if field == "exact_rate" else num(v)
        return "n/a"

    def lat(self, cfg, method, stream, engine, field):
        d = self.s.get("latency", {}).get(f"{cfg}|{method}|{stream}", {})
        v = d.get(engine, {}).get(field)
        return num(v, 2) if isinstance(v, float) else ("n/a" if v is None else num(v))

    def latraw(self, cfg, method, stream, engine, field):
        d = self.s.get("latency", {}).get(f"{cfg}|{method}|{stream}", {})
        return d.get(engine, {}).get(field)

    def engines(self, cfg, method, stream):
        return list(self.s.get("latency", {}).get(f"{cfg}|{method}|{stream}", {}).keys())

    def band(self, src=None):
        d = self.s if src is None else src
        return ((d or {}).get("noise_band") or {}).get("inprocess_aa_max_abs_delta_ns") or 0.0

    def band_med(self):
        return self.s.get("noise_band", {}).get("aa_median_abs_delta_ns") or 0.0

    def band_n(self):
        return self.s.get("noise_band", {}).get("aa_samples") or 0

    def threshold(self, cfg, method, stream):
        """Pre-registered significance threshold: max(2 x in-process A/A band,
        2 x cross-campaign drift of the same engine, 1 ns)."""
        t = 2.0 * self.band()
        if self.ref:
            drift = 0.0
            for e in self.engines(cfg, method, stream):
                a = self.latraw(cfg, method, stream, e, "full_median")
                b = (self.ref.get("latency", {}).get(f"{cfg}|{method}|{stream}", {}).get(e, {})
                     .get("full_median"))
                if a is not None and b is not None:
                    drift = max(drift, abs(a - b))
            t = max(t, 2.0 * drift, 1.0)
            self._drift = drift
        else:
            t = max(t, 1.0)
        return t

    def verdict(self, cfg, method, stream, engine, field="full_median"):
        a = self.latraw(cfg, method, stream, "kieekey", field)
        b = self.latraw(cfg, method, stream, engine, field)
        if a is None or b is None:
            return "n/a"
        d = b - a
        t = self.threshold(cfg, method, stream)
        if abs(d) <= t:
            return f"within noise (|Δ| = {num(abs(d), 2)} ns ≤ threshold {num(t, 2)} ns)"
        if d > 0:
            return (f"yes — KieeKey {num(a, 2)} ns/key vs {num(b, 2)} ns/key is "
                    f"{a and b / a:.2f}× faster, outside the {num(t, 2)} ns threshold")
        return (f"yes — KieeKey {num(a, 2)} ns/key vs {num(b, 2)} ns/key is "
                f"{b and a / b:.2f}× slower, outside the {num(t, 2)} ns threshold")

    def latbest(self, cfg, method, stream, field, want_engine=False):
        best = None
        for e in self.engines(cfg, method, stream):
            if e == "kieekey-aa":
                continue
            v = self.latraw(cfg, method, stream, e, field)
            if v is None:
                continue
            if best is None or v < best[1]:
                best = (e, v)
        if best is None:
            return "n/a"
        return best[0] if want_engine else num(best[1], 2)

    def latgap(self, cfg, method, stream, field="full_median"):
        a = self.latraw(cfg, method, stream, "kieekey", field)
        eng = self.latbest(cfg, method, stream, field, True)
        b = self.latraw(cfg, method, stream, eng, field)
        if a is None or b in (None, 0):
            return "n/a"
        d = 100.0 * (a - b) / b
        t = self.threshold(cfg, method, stream)
        sig = "outside the noise band" if abs(a - b) > t else "inside the noise band"
        return (f"best is {eng} at {num(b, 2)} ns/key; KieeKey {num(a, 2)} ns/key is "
                f"{d:+.1f} % ({'slower' if d > 0 else 'faster'}) — {sig}")

    def corrbest(self, cfg, method, cat, field, want_engine=False):
        best = None
        for r in self.s.get("correctness", []):
            if (r["config"], r["method"], r["cat"]) != (cfg, method, cat):
                continue
            v = r.get(field)
            if v is None:
                continue
            if best is None or v > best[1]:
                best = (r["engine"], v)
        if best is None:
            return "n/a"
        return best[0] if want_engine else rate(best[1])

    def paired(self):
        """Cross-campaign table: headline medians in both campaigns and the
        verdict under the pre-registered two-source band."""
        if not self.ref:
            return "_Run make_report.py with `--ref=<campaign>` to add the " \
                   "cross-campaign (campaign-to-campaign) significance table._"
        rows = [["config", "method", "stream", "engine", "A ns/key", "B ns/key", "Δ A→B (drift)",
                 "Δ vs KieeKey (A)", "threshold", "verdict"]]
        for key in sorted(self.s.get("latency", {})):
            cfg, method, stream = key.split("|", 2)
            t = self.threshold(cfg, method, stream)
            for e in self.engines(cfg, method, stream):
                if e == "kieekey-aa":
                    continue
                a = self.latraw(cfg, method, stream, e, "full_median")
                b = (self.ref.get("latency", {}).get(key, {}).get(e, {}).get("full_median"))
                ka = self.latraw(cfg, method, stream, "kieekey", "full_median")
                ea = self.latraw(cfg, method, stream, e, "full_median")
                rel = (ea - ka) if (ea is not None and ka is not None) else None
                v = "n/a"
                if rel is not None:
                    v = ("within noise" if abs(rel) <= t else
                         ("KieeKey faster" if rel > 0 else "KieeKey slower"))
                drift = abs(a - b) if (a is not None and b is not None) else None
                rows.append([cfg, method, stream, e, num(a, 2), num(b, 2), num(drift, 2),
                             (f"{rel:+.2f}" if rel is not None else "n/a"),
                             num(t, 2), v])
        out = ["| " + " | ".join(rows[0]) + " |", "|" + "|".join(["---"] * len(rows[0])) + "|"]
        out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows[1:]]
        return "\n".join(out)

    # ----------------------------------------------------------------- tokens --
    def resolve(self, token):
        kind, _, arg = token.partition(":")
        if not arg and not kind:
            return None
        parts = arg.split("|")
        if kind == "table":
            # None (not a "MISSING TABLE" string) so the unresolved-token check
            # below fails the build: a table that was never generated must not
            # render as prose that looks like a measurement.
            return self.t.get(parts[0])
        if kind == "meta":
            v = self.s.get("meta", {}).get(parts[0])
            if parts[0] == "t0_unix" and v:
                return datetime.datetime.utcfromtimestamp(int(v)).strftime("%Y-%m-%d %H:%M UTC")
            return "n/a" if v is None else str(v)
        if kind == "env":
            return self.env.get(parts[0], "n/a")
        if kind == "corr":
            return self.corr(*parts[:4], parts[4] if len(parts) > 4 else "exact_rate")
        if kind == "lat":
            return self.lat(parts[0], parts[1], parts[2], parts[3],
                            parts[4] if len(parts) > 4 else "full_median")
        if kind == "latbest":
            return self.latbest(parts[0], parts[1], parts[2],
                                parts[3] if len(parts) > 3 else "full_median", want_engine=True)
        if kind == "latbestval":
            return self.latbest(parts[0], parts[1], parts[2],
                                parts[3] if len(parts) > 3 else "full_median")
        if kind == "latgap":
            return self.latgap(parts[0], parts[1], parts[2],
                               parts[3] if len(parts) > 3 else "full_median")
        if kind == "corrbest":
            return self.corrbest(parts[0], parts[1], parts[2],
                                 parts[3] if len(parts) > 3 else "exact_rate",
                                 want_engine=(len(parts) > 4 and parts[4] == "engine"))
        if kind == "corrval":
            return self.corrbest(parts[0], parts[1], parts[2],
                                 parts[3] if len(parts) > 3 else "exact_rate")
        if kind == "verdict":
            return self.verdict(parts[0], parts[1], parts[2], parts[3])
        if kind in ("band",) and not arg:
            return num(self.band(), 2)
        if kind in ("bandref",) and not arg:
            return num(self.band(self.ref), 2)
        if kind == "band_med" and not arg:
            return num(self.band_med(), 2)
        if kind == "band_n" and not arg:
            return num(self.band_n())
        if kind == "threshold":
            return num(self.threshold(parts[0], parts[1], parts[2]), 2)
        if kind == "ext":
            # {{ext:relative/path::regex::group}} — reads a number out of an
            # existing repo artifact (e.g. docs/bench/...) at report time, so it
            # is still not hand-typed. The regex is visible in the narrative.
            path, pat, grp = arg.split("::")
            txt = open(os.path.join(ROOT, path), encoding="utf-8", errors="replace").read()
            m = re.search(pat, txt, re.S)
            if not m:
                return f"MISSING[{path}]"
            g = int(grp) if grp.isdigit() else 1
            return m.group(g)
        if kind == "stat":
            node = self.s
            for part in arg.split("."):
                if isinstance(node, dict):
                    node = node.get(part)
                elif isinstance(node, list):
                    # list[<key>=<value>].<field> selects a row
                    sel, _eq, want = part.partition("=")
                    row = None
                    for it in node:
                        if isinstance(it, dict) and str(it.get(sel)) == want:
                            row = it
                    node = row
                else:
                    node = None
            if isinstance(node, dict):
                return ", ".join(f"{k}={v}" for k, v in sorted(node.items()))
            if isinstance(node, float):
                return num(node, 2)
            if node is None and getattr(self, "strict_stat", False):
                return None
            return "n/a" if node is None else str(node)
        if kind == "gate":
            return self.gate(arg)
        if kind == "gatesummary" and not arg:
            return self.gates_summary()
        if kind == "cell":
            cfg, method, stream, engine, field = arg.split("|")
            return self.cell(f"{cfg}|{method}|{stream}", engine, field)
        if kind == "dcell":
            cfg, method, stream, field = arg.split("|")
            return self.dcell(f"{cfg}|{method}|{stream}", field)
        if kind == "gaincell":
            cfg, method, stream, field = arg.split("|")
            return self.gaincell(f"{cfg}|{method}|{stream}", field)
        if kind == "l2cell":
            cfg, method, stream, engine, field = arg.split("|")
            return self.l2cell(f"{cfg}|{method}|{stream}", engine, field)
        if kind == "regressing":
            return self.regressing(int(arg) if arg.isdigit() else None)
        if kind == "statlist":
            node = self.s
            for part in arg.split("."):
                node = node.get(part) if isinstance(node, dict) else None
            if node is None:
                return "n/a"
            if isinstance(node, list):
                return " · ".join(str(x).replace("|", " / ") for x in node) or "none"
            return str(node)
        if kind == "statcount":
            node = self.s
            for part in arg.split("."):
                node = node.get(part) if isinstance(node, dict) else None
            return str(len(node) if isinstance(node, (list, dict)) else 0)
        if kind == "paired":
            return self.paired()
        if kind == "campaign":
            return self.env.get("campaign", "n/a")
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--campaign", required=True)
    ap.add_argument("--ref", default="", help="second campaign dir name, for the noise band")
    ap.add_argument("--out", default="benchmark/REPORT.md")
    ap.add_argument("--narrative", default="benchmark/REPORT.narrative.md",
                    help="template with the prose; the RC1 report uses "
                         "benchmark/REPORT.rc1.narrative.md")
    ap.add_argument("--strict-stat", action="store_true",
                    help="an unresolvable {{stat:…}} path fails the build instead of "
                         "rendering as 'n/a' — the RC1 report uses this, because 'n/a' "
                         "in a prose sentence reads uncomfortably like a measurement")
    a = ap.parse_args()

    res = os.path.join(ROOT, "benchmark", "results", a.campaign)
    refdir = os.path.join(ROOT, "benchmark", "results", a.ref) if a.ref else None
    narr = os.path.join(ROOT, a.narrative)
    if not os.path.exists(narr):
        sys.exit(f"missing {narr}")
    rep = Report(res, refdir)
    rep.strict_stat = a.strict_stat
    text = open(narr, encoding="utf-8").read()

    missing = []

    def sub(m):
        v = rep.resolve(m.group(1))
        if v is None:
            missing.append(m.group(1))
            return m.group(0)
        return v

    body = re.sub(r"\{\{([a-zA-Z_]+(?::[^{}]*)?)\}\}", sub, text)
    hdr = ("<!-- GENERATED by benchmark/scripts/make_report.py from "
           f"benchmark/results/{a.campaign} — do not edit numbers by hand. -->\n")
    if missing:
        print(f"[report] FAILED: unresolved tokens: {sorted(set(missing))}", file=sys.stderr)
        return 1
    with open(os.path.join(ROOT, a.out), "w", encoding="utf-8") as f:
        f.write(hdr + body.rstrip("\n") + "\n")
    tbl_lines = len(re.findall(r"^\|", body, re.M))
    print(f"[report] wrote {a.out} from {a.campaign}"
          + (f" (noise reference {a.ref})" if a.ref else "")
          + f"; {tbl_lines} table lines in the rendered page")
    return 0


if __name__ == "__main__":
    sys.exit(main())
