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
#   KieeKey - refactored and completed logic
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
# File: tests/run_isolation_ab.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#----------------------------------------------------------------------------
# tests/run_isolation_ab.sh — v1.3.0-beta6 (G4): DETERMINISTIC A/B campaign
# for tests/bench_v130_isolation.cpp.
#
# beta5 ran this campaign by hand (see docs/bench/beta5/BENCHMARK_REPORT.md,
# "Result 2"): build the harness from two trees, 15 alternating 300 000-key
# rounds, order swapped per pair, pinned core, medians. This script is that
# procedure as code, so the campaign is reproducible byte-for-byte by CI
# (v1.3.0-beta6 moves it to a GitHub Actions runner at the user's request)
# and so both sides are built IDENTICALLY (same flags, same harness source).
#
# The harness file is taken from the CURRENT tree and compiled into BOTH
# binaries (only the sources under test come from the base tree), which
# makes a harness-drift false positive impossible by construction.
#
# Usage:
#   tests/run_isolation_ab.sh --base=/path/to/base/worktree \
#       [--side=beta6] [--rounds=15] [--keys=300000] [--core=1] \
#       [--cxx=g++] [--out=DIR]
#
# Output: $OUT/iso_<side>.json + $OUT/iso_<side>_report.txt + per-round logs.
# Per-round "VERDICT FAIL" lines from the harness are EXPECTED under host
# interference and are kept in the logs — the campaign verdict is the median.
#
# Exit: 0 = campaign completed (median table written; the REPORT interprets
#       deltas), 2 = setup/build error.
#----------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

CXX="${CXX:-g++}"
BASE=""
SIDE="cur"
ROUNDS=15
KEYS=300000
CORE=1
OUT=""

for arg in "$@"; do
    case "$arg" in
        --base=*)   BASE="${arg#--base=}" ;;
        --side=*)   SIDE="${arg#--side=}" ;;
        --rounds=*) ROUNDS="${arg#--rounds=}" ;;
        --keys=*)   KEYS="${arg#--keys=}" ;;
        --core=*)   CORE="${arg#--core=}" ;;
        --cxx=*)    CXX="${arg#--cxx=}" ;;
        --out=*)    OUT="${arg#--out=}" ;;
        *) echo "run_isolation_ab.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done

[ -n "$BASE" ] || { echo "run_isolation_ab.sh: --base=<worktree> is required" >&2; exit 2; }
BASE="$(cd "$BASE" && pwd)"
[ -n "$OUT" ] || OUT="$REPO_ROOT/bench_runs/isolation-${SIDE}-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT/logs"

HARNESS="$SCRIPT_DIR/bench_v130_isolation.cpp"
SOURCES=(TextEngine.cpp Arcade.cpp ArcadeFrame.cpp ArcadeRender.cpp ChaosEngine.cpp
         AiRival.cpp Progression.cpp TypingAnalytics.cpp)
FLAGS="-std=c++2b -O2 -pthread"

# Pinning: taskset if available (CI runners and dev sandboxes have it; fall
# back to unpinned with a loud note rather than failing the campaign).
PIN=(taskset -c "$CORE")
if ! command -v taskset >/dev/null 2>&1; then
    echo "[iso] WARNING: taskset not found — rounds run UNPINNED (noise band widens)"
    PIN=()
fi

echo "[iso] base   : $BASE ($(git -C "$BASE" describe --tags --always 2>/dev/null || echo '?'))"
echo "[iso] cur    : $REPO_ROOT ($(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo '?'))"
echo "[iso] rounds : $ROUNDS x $KEYS keys, core=$CORE, cxx=$CXX"

# --- build both binaries (identical harness + flags; sources differ) --------
build_side() { # $1 = tree root, $2 = binary name
    local tree="$1" bin="$2" objs=() src
    for src in "${SOURCES[@]}"; do
        objs+=("$tree/src/core/$src")
    done
    if "$CXX" $FLAGS -I"$tree/src/core" -I"$tree/tests" "$HARNESS" "${objs[@]}" \
            -o "$OUT/bin_$bin" 2> "$OUT/logs/build_$bin.log"; then
        echo "[iso] build $bin ok"
    else
        echo "[iso] build $bin FAILED — see $OUT/logs/build_$bin.log" >&2
        cat "$OUT/logs/build_$bin.log" >&2
        exit 2
    fi
}
build_side "$BASE" base
build_side "$REPO_ROOT" "$SIDE"

# --- alternating rounds ------------------------------------------------------
TSV="$OUT/rounds.tsv"
: > "$TSV"
for ((r = 1; r <= ROUNDS; r++)); do
    # Order swapped per pair: odd rounds run base first, even rounds cur first.
    if (( r % 2 == 1 )); then ORDER=(base "$SIDE"); else ORDER=("$SIDE" base); fi
    for which in "${ORDER[@]}"; do
        log="$OUT/logs/round_$(printf '%02d' "$r")_$which.log"
        "${PIN[@]}" "$OUT/bin_$which" --keys="$KEYS" > "$log" 2>&1 || true
        # TSV: round side cfg mean p50 p90 p99 digest
        # Row layout (setw columns): name... mean p50 p90 p99 delta digest —
        # the name contains spaces, so anchor fields from the END: digest=$NF,
        # delta=$(NF-1), p99=$(NF-2), p90=$(NF-3), p50=$(NF-4), mean=$(NF-5).
        awk -v r="$r" -v s="$which" '
            /^[0-9]\. / {
                name=$1; mean=$(NF-5); p50=$(NF-4); p90=$(NF-3); p99=$(NF-2);
                dig=$NF;
                printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", r, s, name, mean, p50, p90, p99, dig
            }' "$log" >> "$TSV"
    done
    echo "[iso] round $r/$ROUNDS done"
done

# --- aggregate medians + report ----------------------------------------------
python3 - "$TSV" "$OUT" "$SIDE" "$BASE" "$ROUNDS" "$KEYS" <<'PYEOF'
import json, statistics, sys, time, pathlib, subprocess

tsv, outdir, side, base, rounds, keys = sys.argv[1:7]
outdir = pathlib.Path(outdir)

def sh(cmd, cwd=None):
    try:
        return subprocess.run(cmd, shell=True, capture_output=True, text=True,
                              timeout=10, cwd=cwd).stdout.strip()
    except Exception:
        return ""

rows = []
for line in pathlib.Path(tsv).read_text().splitlines():
    f = line.split("\t")
    if len(f) != 8:
        continue
    rnd, who, cfg = f[0], f[1], f[2]
    rows.append((cfg, who, float(f[3]), float(f[4]), float(f[5]), float(f[6]), f[7]))

cfgs = sorted({r[0] for r in rows})
NAMES = {
    "1.": "1. Pure IME baseline (control)",
    "2.": "2. + Inactive Arcade (standby)",
    "3.": "3. + Active Chaos Engine",
    "4.": "4. + Active AI telemetry",
    "5.": "5. + Full v1.3.0 suite",
}
sides = ["base", side]
table, digests_equal = [], True
for cfg in cfgs:
    per = {}
    for who in sides:
        vals = [(m, p50, p90, p99, dig) for (c, w, m, p50, p90, p99, dig) in rows
                if c == cfg and w == who]
        per[who] = {
            "rounds": len(vals),
            "median_mean": statistics.median([v[0] for v in vals]),
            "median_p50": statistics.median([v[1] for v in vals]),
            "median_p90": statistics.median([v[2] for v in vals]),
            "median_p99": statistics.median([v[3] for v in vals]),
            "digests": sorted({v[4] for v in vals}),
        }
    b, c = per["base"], per[side]
    dig_ok = b["digests"] == c["digests"] and len(b["digests"]) == 1
    digests_equal &= dig_ok
    dp50 = (c["median_p50"] - b["median_p50"]) / b["median_p50"] * 100.0 if b["median_p50"] else 0.0
    table.append({
        "configuration": NAMES.get(cfg, cfg),
        "configuration_key": cfg,
        "base_median_p50_ns": b["median_p50"], "cur_median_p50_ns": c["median_p50"],
        "delta_p50_percent": round(dp50, 2),
        "base_median_mean_ns": b["median_mean"], "cur_median_mean_ns": c["median_mean"],
        "base_median_p99_ns": b["median_p99"], "cur_median_p99_ns": c["median_p99"],
        "digests_identical": dig_ok,
        "digest": b["digests"][0] if len(b["digests"]) == 1 else b["digests"],
    })

result = {
    "schema": "kieekey.isolation_ab.v1",
    "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "base": {"path": base, "describe": sh("git describe --tags --always", base)},
    "side_label": side,
    "side_commit": sh("git rev-parse HEAD"),
    "rounds": int(rounds), "keys_per_round": int(keys),
    "rows_per_round": len(rows) // int(rounds),
    "digests_identical_all": digests_equal,
    "results": table,
}
(outdir / f"iso_{side}.json").write_text(json.dumps(result, indent=1))

lines = []
lines.append(f"Feature isolation A/B — {result['base']['describe']} (base) vs {side}")
lines.append(f"{rounds} alternating rounds x {keys} keys; medians; per-round logs in logs/")
lines.append("")
lines.append("| Configuration | base p50 | cur p50 | Δp50 | base mean | cur mean | digests |")
lines.append("|---|---|---|---|---|---|---|")
for t in table:
    lines.append("| {} | {:.0f} | {:.0f} | {:+.1f} % | {:.1f} | {:.1f} | {} |".format(
        t["configuration"], t["base_median_p50_ns"], t["cur_median_p50_ns"],
        t["delta_p50_percent"], t["base_median_mean_ns"], t["cur_median_mean_ns"],
        "identical" if t["digests_identical"] else "DIFFER"))
lines.append("")
lines.append("digests identical everywhere: " + ("YES" if digests_equal else "NO — investigate"))
report = "\n".join(lines)
(outdir / f"iso_{side}_report.txt").write_text(report + "\n")
print(report)
PYEOF
echo "[iso] artifacts: $OUT/iso_${SIDE}.json, $OUT/iso_${SIDE}_report.txt"
