#!/usr/bin/env bash
#============================================================================
# KieeKey - A modified version based on OpenKey
#
# Original work:
#   OpenKey - Vietnamese input method engine
#   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
#   Licensed under the GNU General Public License version 3.
#
# Modified work:
#   KieeKey v1.2.0 - refactored and completed logic
#   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
#   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# File: tests/run_bench_suite.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#----------------------------------------------------------------------------
# tests/run_bench_suite.sh — KieeKey benchmark suite, one command.
#
# Runs, in order, with deterministic build flags and a machine-readable
# environment record:
#   1. correctness gate  (tests/gate_correctness.cpp — engine vs clean-room
#                         oracle; 2.06M events). Latency runs are NOT
#                         executed if the gate fails for a side.
#   2. micro layer       (imebench_kit/harness/bench_perf.cpp v2 — engine
#                         decision latency, per-key steady_clock, warm-up,
#                         session reset, T0-control overhead floor).
#   3. integration layer (tests/e2e_bench.cpp — shim Win32-wrapper pipeline:
#                         OutputRing + wake ring + InlineEmitter).
#   4. tone layer        (tests/bench_tone_latency.cpp — 8 frozen tone
#                         populations + paced mixed passage; the binary
#                         self-gates composition every run).
#
# Usage:
#   tests/run_bench_suite.sh [options]
#
# Options:
#   --side=LABEL        side label (default: short git sha of HEAD)
#   --runs=N            independent interleaved runs per side (default 3)
#   --perf-keys=N       micro keys per workload per run (default 2000000)
#   --perf-warmup=N     micro warm-up keys per workload (default 200000)
#   --e2e-keys=N        integration keys per run (default 100000)
#   --tone-extra="..."  extra args for the tone bench
#                       (default "--pop=all --path=inline --iters=500 --mixed-reps=1")
#   --out=DIR           artifact directory (default
#                       bench_runs/<side>-<sha>-<UTC timestamp> under the
#                       repo root; results are regenerable and are NOT meant
#                       to be committed — see docs/reports/README.md policy)
#   --ab-base=DIR       second tree (previous KieeKey version) to interleave
#                       A/B, e.g. a checkout of the v1.1.3 tag. The harness
#                       sources of the base tree are synced from this tree
#                       first so the A/B isolates the engine.
#   --no-tone           skip the tone-population pass (fast smoke)
#
# Environment: CXX (default g++); PYTHON (default python3).
# Run from anywhere; the script cd's to the repo root. Exit 0 only when the
# gate PASSes on every side and every benchmark run completes.
#----------------------------------------------------------------------------
set -euo pipefail

# --- locate repo root -------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

CXX="${CXX:-g++}"
PY="${PYTHON:-python3}"

# --- defaults ---------------------------------------------------------------
SIDE=""
RUNS=3
PERF_KEYS=2000000
PERF_WARMUP=200000
E2E_KEYS=100000
TONE_EXTRA="--pop=all --path=inline --iters=500 --mixed-reps=1"
OUT=""
AB_BASE=""
NO_TONE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --side=*)      SIDE="${1#--side=}" ;;
    --runs=*)      RUNS="${1#--runs=}" ;;
    --perf-keys=*) PERF_KEYS="${1#--perf-keys=}" ;;
    --perf-warmup=*) PERF_WARMUP="${1#--perf-warmup=}" ;;
    --e2e-keys=*)  E2E_KEYS="${1#--e2e-keys=}" ;;
    --tone-extra=*) TONE_EXTRA="${1#--tone-extra=}" ;;
    --out=*)       OUT="${1#--out=}" ;;
    --ab-base=*)   AB_BASE="${1#--ab-base=}" ;;
    --no-tone)     NO_TONE=1 ;;
    *) echo "run_bench_suite.sh: unknown arg $1" >&2; exit 2 ;;
  esac
  shift
done

COMMIT="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
COMMIT_SHORT="${COMMIT:0:10}"
DESCRIBE="$(git describe --tags --always 2>/dev/null || echo "$COMMIT_SHORT")"
[ -z "$SIDE" ] && SIDE="$COMMIT_SHORT"
TS="$(date -u +%Y%m%dT%H%M%SZ)"
[ -z "$OUT" ] && OUT="$REPO_ROOT/bench_runs/${SIDE}-${COMMIT_SHORT}-${TS}"
mkdir -p "$OUT/bin"

echo "[suite] KieeKey benchmark suite"
echo "[suite] commit=$COMMIT describe=$DESCRIBE side=$SIDE runs=$RUNS"
echo "[suite] out=$OUT"

# --- environment metadata record (#1) --------------------------------------
"$PY" - "$OUT/env.json" "$CXX" "$COMMIT" "$DESCRIBE" "$SIDE" "$RUNS" <<'PYEOF'
import json, os, platform, re, subprocess, sys, time
out, cxx, commit, describe, side, runs = sys.argv[1:7]
def sh(cmd):
    try:
        return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=15).stdout.strip()
    except Exception:
        return ""
cpu_model = sh("grep -m1 'model name' /proc/cpuinfo") or "unavailable"
cpu_mhz   = sh("grep -m1 'cpu MHz' /proc/cpuinfo") or "unavailable"
mem       = sh("grep -m1 MemTotal /proc/meminfo") or "unavailable"
compiler  = sh(cxx + " --version | head -1") or "unavailable"
env = {
  "schema": "run_bench_suite.env.v1",
  "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
  "side_label": side,
  "git": {"commit": commit, "describe": describe},
  "build": {
    "compiler": compiler,
    "flags": {
      "gate": "-std=c++17 -O2 -DNO_205",
      "bench_perf": "-std=c++17 -O3 -DNDEBUG",
      "e2e_bench": "-std=c++20 -O2 -DOK_WRAP_NO_WIN32",
      "bench_tone_latency": "-std=c++23 -O2 -DOK_WRAP_NO_WIN32"
    },
    "cmake": "unavailable in this shell (unit tests compiled directly)",
    "config": "Release-equivalent (-O3/-O2, NDEBUG for perf)"
  },
  "host": {
    "os": platform.system() + " " + (platform.release() or ""),
    "arch": platform.machine(),
    "cpu_model": cpu_model,
    "cpu_mhz": cpu_mhz,
    "logical_cpus": os.cpu_count(),
    "physical_cores": "unavailable (no lscpu)",
    "ram": mem,
    "power_source": "unavailable (virtualized host; no AC/battery data)",
    "hypervisor": sh("grep -m1 'Hypervisor vendor' /proc/cpuinfo") or "unavailable",
    "kernel": sh("uname -sr") or "unavailable"
  },
  "timer": {
    "clock": "std::chrono::steady_clock (shim qpcNs wrapper) — CLOCK_MONOTONIC on Linux",
    "overhead_measured": "T0-control no-op loop inside bench_perf (reported, never subtracted)"
  },
  "measurement_scope": {
    "measurable": [
      "engine decision latency (platform-independent core)",
      "shim E2E pipeline latency (win32_wrapper deterministic shim)",
      "tone-population latency through the shim pipeline",
      "engine-vs-oracle differential correctness"
    ],
    "not_measurable_here": [
      "real Windows SendInput/registry/TSF/WinUI surfaces",
      "end-to-end latency through the real Windows input stack",
      "AC-power / turbo / thermal state"
    ]
  },
  "process_control": {
    "priority": "SCHED_FIFO attempted by e2e_bench (root only); reported per run",
    "affinity": "e2e_bench pins producer/consumer to cores 0/1 when >=2 CPUs",
    "notes": "shared KVM host: run-to-run scheduler noise is expected and is why every benchmark uses interleaved multi-run medians with spreads"
  }
}
json.dump(env, open(out, "w"), indent=1)
print("env record:", out)
PYEOF

# --- build + gate + run helpers --------------------------------------------
# trees to test: current always; base when --ab-base given
declare -a TREES=("$REPO_ROOT:cur")
if [ -n "$AB_BASE" ]; then
  AB_BASE="$(cd "$AB_BASE" && pwd)"
  # sync harness sources so the A/B isolates the engine (harness files are
  # byte-identical between versions by design; copy over any drift)
  for hf in imebench_kit/harness/bench_perf.cpp tests/e2e_bench.cpp \
            tests/bench_tone_latency.cpp tests/gate_correctness.cpp; do
    mkdir -p "$AB_BASE/$(dirname "$hf")"
    cp "$REPO_ROOT/$hf" "$AB_BASE/$hf"
  done
  TREES+=("$AB_BASE:base")
fi

build_side() { # $1=tree  $2=tag(cur|base)
  local T="$1" TAG="$2" B="$OUT/bin"
  echo "[suite] building $TAG from $T"
  "$CXX" -std=c++17 -O2 -DNO_205 -Isrc/core -Itests -Iimebench_kit/harness \
      tests/gate_correctness.cpp src/core/TextEngine.cpp -o "$B/gate_$TAG"
  "$CXX" -std=c++17 -O3 -DNDEBUG -Isrc/core -Iimebench_kit/harness \
      imebench_kit/harness/bench_perf.cpp src/core/TextEngine.cpp -o "$B/perf_$TAG"
  "$CXX" -std=c++20 -O2 -Isrc/core -Itests -DOK_WRAP_NO_WIN32 \
      tests/e2e_bench.cpp src/core/win32_wrapper.cpp src/core/TextEngine.cpp -o "$B/e2e_$TAG"
  "$CXX" -std=c++23 -O2 -Isrc/core -Itests -DOK_WRAP_NO_WIN32 \
      tests/bench_tone_latency.cpp src/core/win32_wrapper.cpp src/core/TextEngine.cpp -o "$B/tone_$TAG"
}

for entry in "${TREES[@]}"; do
  T="${entry%%:*}"; TAG="${entry##*:}"
  ( cd "$T" && build_side "$T" "$TAG" )
done

# --- correctness gate (separate from latency; abort on FAIL) ---------------
OVERALL=0
for entry in "${TREES[@]}"; do
  T="${entry%%:*}"; TAG="${entry##*:}"
  echo "[suite] gate $TAG ..."
  set +e
  ( cd "$T" && "$OUT/bin/gate_$TAG" ) > "$OUT/gate_${TAG}.log" 2>&1
  RC=$?
  set -e
  echo "$RC" > "$OUT/gate_${TAG}.rc"
  if [ $RC -ne 0 ]; then
    echo "[suite] GATE FAIL for $TAG (rc=$RC) — latency numbers would be untrustworthy; aborting." >&2
    OVERALL=1
  else
    echo "[suite] gate $TAG PASS"
  fi
done
[ $OVERALL -eq 1 ] && { echo "[suite] suite aborted: correctness gate failed" >&2; exit 1; }

# --- micro layer: interleaved runs -----------------------------------------
for ((r=1; r<=RUNS; r++)); do
  for entry in "${TREES[@]}"; do
    T="${entry%%:*}"; TAG="${entry##*:}"
    if [ "$TAG" = base ]; then SEED=$((2000 + r)); else SEED=$((1000 + r)); fi
    ( cd "$T" && "$OUT/bin/perf_$TAG" --keys="$PERF_KEYS" --warmup="$PERF_WARMUP" \
        --runs=1 --seed=$SEED \
        --out="$OUT/perf_${TAG}_${r}.json" --label="${TAG}-${COMMIT_SHORT}" ) \
        > "$OUT/perf_${TAG}_${r}.log" 2>&1
    echo "[suite] perf $TAG run $r done"
  done
done

# --- integration layer: interleaved runs ------------------------------------
# NOTE: e2e_bench exits 1 when the burst-mode target gates (p50<=2.5us,
# p99<=7us — defined for the real-time Windows path) are not met. On shared /
# virtualized hosts that is a host-capability verdict, NOT a benchmark
# failure, so its rc is recorded but does not abort the suite.
for ((r=1; r<=RUNS; r++)); do
  for entry in "${TREES[@]}"; do
    T="${entry%%:*}"; TAG="${entry##*:}"
    set +e
    ( cd "$T" && "$OUT/bin/e2e_$TAG" --keys="$E2E_KEYS" --runs=1 \
        --json="$OUT/e2e_${TAG}_${r}.json" ) > "$OUT/e2e_${TAG}_${r}.log" 2>&1
    ERC=$?
    set -e
    echo "$ERC" > "$OUT/e2e_${TAG}_${r}.rc"
    echo "[suite] e2e $TAG run $r done (rc=$ERC; 1 = burst target gates not met on this host)"
  done
done

# --- tone layer (single pass per side; self-gated composition) --------------
if [ $NO_TONE -eq 0 ]; then
  for entry in "${TREES[@]}"; do
    T="${entry%%:*}"; TAG="${entry##*:}"
    echo "[suite] tone $TAG (this is the long pole; ~1-2 min/side) ..."
    ( cd "$T" && "$OUT/bin/tone_$TAG" $TONE_EXTRA ) > "$OUT/tone_${TAG}.log" 2>&1
    echo "[suite] tone $TAG done"
  done
fi

# --- aggregate into summary.json + summary.txt ------------------------------
"$PY" - "$OUT" "$RUNS" "$NO_TONE" <<'PYEOF'
import json, os, re, statistics, sys
outdir, runs, no_tone = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
def med(v):
    v = sorted(v); return v[len(v)//2]
sides = sorted({f.split("_")[1] for f in os.listdir(outdir) if f.startswith("perf_") and f.endswith(".json")})
summary = {"schema": "run_bench_suite.summary.v1", "sides": {}}
for side in sides:
    s = {"perf": {}, "e2e": {}, "tone": {}, "gate": {}}
    # gate
    try:
        s["gate"] = {"rc": int(open(f"{outdir}/gate_{side}.rc").read().strip()),
                     "log": f"gate_{side}.log"}
    except Exception:
        s["gate"] = {"rc": -1}
    # perf: median across runs of each workload tier metric
    pfs = [json.load(open(f"{outdir}/perf_{side}_{r}.json")) for r in range(1, runs+1)]
    ctl = [p["run_records"][0]["T0_control_ns"] for p in pfs]
    s["perf"]["T0_control_ns"] = {m: med([c[m] for c in ctl]) for m in ("min","mean","p50","p99","max")}
    for w in pfs[0]["run_records"][0]["workloads"]:
        wname = w["name"]
        s["perf"].setdefault(wname, {})
        for tier in w["tiers"]:
            tn = tier["tier"]
            acc = {}
            for m in ("min","mean","p50","p90","p99","p999","max"):
                vals = [p["run_records"][0]["workloads"][i]["tiers"][j][m]
                        for p in pfs for i in range(len(p["run_records"][0]["workloads"]))
                        for j in range(2)
                        if p["run_records"][0]["workloads"][i]["name"]==wname
                        and p["run_records"][0]["workloads"][i]["tiers"][j]["tier"]==tn]
                acc[m] = med(vals)
            s["perf"][wname][tn] = acc
    # e2e
    if os.path.exists(f"{outdir}/e2e_{side}_1.json"):
        ers = [json.load(open(f"{outdir}/e2e_{side}_{r}.json"))["runs"][0] for r in range(1, runs+1)]
        def agg(key, inner):
            return {m: med([x[key][m] for x in ers]) for m in inner}
        s["e2e"]["raw_us"] = agg("raw_us", ("p50","p99","p999","max"))
        s["e2e"]["burst_hot_us"] = agg("burst_hot_us", ("p50","p99","p999","max"))
        s["e2e"]["pipeline_us"] = agg("pipeline_us", ("p50","p99","p999","max"))
        s["e2e"]["wake_pay_us"] = agg("wake_pay_us", ("p50","p99"))
        s["e2e"]["lag_spikes_median"] = med([x["lag_spikes_keys"] for x in ers])
        s["e2e"]["throughput_keys_per_s_median"] = med([x["throughput_keys_per_s"] for x in ers])
        s["e2e"]["peak_rss_mb_total_median"] = med([x["peak_rss_mb_total"] for x in ers])
        s["e2e"]["burst_p50_target_ok"] = med([x["burst_hot_us"]["p50"] for x in ers]) <= 2.5
        s["e2e"]["burst_p99_target_ok"] = med([x["burst_hot_us"]["p99"] for x in ers]) <= 7.0
    # tone: parse the log (fixed text format)
    tl = f"{outdir}/tone_{side}.log"
    if os.path.exists(tl):
        cur = None
        for line in open(tl):
            m = re.match(r"\[pop\] (\S+)\s+path=\S+\s+barrier=(\S+)", line)
            if m: cur = (m.group(1), m.group(2)); continue
            if not cur: continue
            m2 = re.search(r"E2E\(t5\) p50=\s*([\d.]+) p99=\s*([\d.]+)", line)
            if m2:
                s["tone"].setdefault(cur[0], {})[cur[1]] = {"edit_p50_us": float(m2.group(1)), "edit_p99_us": float(m2.group(2))}
    summary["sides"][side] = s
json.dump(summary, open(f"{outdir}/summary.json", "w"), indent=1)
# human table
L = []
L.append("=== summary (medians over %d run(s)) ===" % runs)
for side in sides:
    s = summary["sides"][side]
    L.append(f"[{side}] gate rc={s['gate']['rc']}")
    c = s["perf"]["T0_control_ns"]
    L.append(f"[{side}] T0-control ns mean={c['mean']:.1f} p50={c['p50']} max={c['max']}")
    L.append(f"[{side}] perf T1-decision p50 ns: " + ", ".join(
        f"{w}={s['perf'][w]['T1-decision']['p50']:.0f}" for w in s['perf'] if 'T1-decision' in s['perf'][w]))
    if s["e2e"]:
        e = s["e2e"]
        L.append(f"[{side}] e2e burst/hot p50={e['burst_hot_us']['p50']:.2f}us p99={e['burst_hot_us']['p99']:.2f}us "
                 f"pipeline p50={e['pipeline_us']['p50']:.2f}us wake p50={e['wake_pay_us']['p50']:.2f}us "
                 f"spikes-med={e['lag_spikes_median']} rss={e['peak_rss_mb_total_median']:.2f}MB")
    if s["tone"]:
        rows = [f"{p}[{b}]={v['edit_p50_us']:.3f}" for p in sorted(s["tone"]) for b, v in sorted(s["tone"][p].items())]
        L.append(f"[{side}] tone edit p50 us (pop[barrier]): " + " ".join(rows))
open(f"{outdir}/summary.txt", "w").write("\n".join(L) + "\n")
print("\n".join(L))
PYEOF

echo "[suite] artifacts in $OUT"
ls -1 "$OUT"
[ $OVERALL -eq 0 ] && echo "[suite] VERDICT: PASS" || echo "[suite] VERDICT: FAIL"
exit $OVERALL
