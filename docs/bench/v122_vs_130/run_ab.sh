#!/usr/bin/env bash
#----------------------------------------------------------------------------
# docs/bench/v122_vs_130/run_ab.sh — apple-to-apple A/B of the SHIPPED typing
# engine between the v1.2.2 stable tag and the current tree.
#
#   usage: bash docs/bench/v122_vs_130/run_ab.sh [rounds]     (default 3)
#
# What makes it apple-to-apple:
#   * the three harnesses (bench_three_engines, e2e_bench, bench_engine) are
#     byte-identical in both trees — the script REFUSES to run when their git
#     blob ids differ, because that would compare harnesses, not engines;
#   * same compiler and flags for both trees, built into separate worktrees;
#   * interleaved rounds (baseline -> candidate) so host-load drift hits both;
#   * medians, printed as the summary this directory's README quotes.
#
# Evidence from a real run of this script: summary.txt (and README.md, which
# explains the host caveat: e2e_bench's absolute targets are Windows-only).
#----------------------------------------------------------------------------
set -uo pipefail
ROUNDS="${1:-3}"
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
BASE_TAG="${BASE_TAG:-v1.2.2}"
WORK="${WORK:-/tmp/kieekey-ab}"
BASE="$WORK/base"
HEAD="$WORK/head"
OUT="$WORK/out"

mkdir -p "$OUT"
cd "$REPO" || exit 1

echo "[ab] repo   : $REPO"
echo "[ab] baseline: $BASE_TAG (added as a detached worktree in $BASE)"
echo "[ab] candidate: the current working tree ($HEAD)"

git worktree remove --force "$BASE" 2>/dev/null
git worktree remove --force "$HEAD" 2>/dev/null
git worktree add --detach "$BASE" "$BASE_TAG" >/dev/null || exit 1
git worktree add --detach "$HEAD" HEAD >/dev/null || exit 1

# ---- harness identity gate -------------------------------------------------
HARNESSES="tests/bench_three_engines.cpp tests/e2e_bench.cpp tests/bench_engine.cpp"
base_ids=$(cd "$BASE" && git hash-object $HARNESSES | tr '\n' ' ')
head_ids=$(cd "$HEAD" && git hash-object $HARNESSES | tr '\n' ' ')
if [ "$base_ids" != "$head_ids" ]; then
    echo "[ab] REFUSING: the harnesses differ between the trees"
    echo "     baseline : $base_ids"
    echo "     candidate: $head_ids"
    exit 2
fi
echo "[ab] harness blob ids match: $base_ids"

# ---- build both trees ------------------------------------------------------
for tree in "$BASE" "$HEAD"; do
    ( cd "$tree" || exit 1
      g++ -std=c++23 -O2 -Isrc/core tests/bench_engine.cpp src/core/TextEngine.cpp \
          -o "$OUT/ok_bench_$(basename "$tree")" -pthread
      g++ -std=c++20 -O2 -Isrc/core -Itests -DOK_WRAP_NO_WIN32 tests/e2e_bench.cpp \
          src/core/win32_wrapper.cpp src/core/TextEngine.cpp \
          -o "$OUT/ok_e2e_$(basename "$tree")" -pthread ) || exit 1
done

# ---- interleaved rounds ----------------------------------------------------
for i in $(seq 1 "$ROUNDS"); do
    for tree in "$BASE" "$HEAD"; do
        name=$(basename "$tree")
        ( cd "$tree" && bash tests/run_three_bench.sh >"$OUT/three_${name}_r$i.log" 2>&1 )
        cp "$tree/THREE_ENGINE_BENCH_REPORT.md" "$OUT/three_${name}_r$i.md" 2>/dev/null
        "$OUT/ok_bench_$name" > "$OUT/bench_engine_${name}_r$i.txt" 2>&1
        # ok_e2e_bench exits 1 on a Linux host (scheduler deschedules) — the
        # numbers are still written to the JSON; that is the point of the A/B.
        "$OUT/ok_e2e_$name" --keys=100000 --runs=3 --json="$OUT/e2e_${name}_r$i.json" \
            > "$OUT/e2e_${name}_r$i.txt" 2>&1
        echo "[ab] round $i done for $name"
    done
done

# ---- aggregate -------------------------------------------------------------
python3 - "$OUT" "$BASE_TAG" <<'PY'
import glob, json, os, re, statistics as st, sys

out, base_tag = sys.argv[1], sys.argv[2]


def med(values):
    return st.median(values)


def three(d):
    rows = []
    for f in sorted(glob.glob(os.path.join(out, 'three_%s_r*.md' % d))):
        for line in open(f, encoding='utf-8'):
            if line.startswith('| KieeKey'):
                cells = [c.strip() for c in line.strip('|').split('|')]
                if len(cells) >= 7 and cells[1].replace('.', '').isdigit():
                    rows.append([float(x) for x in cells[1:6]] + [int(cells[6])])
    return [med(c) for c in zip(*rows)] if rows else []


def e2e(d):
    metrics, ctl = {}, {}
    for f in sorted(glob.glob(os.path.join(out, 'e2e_%s_r*.json' % d))):
        doc = json.load(open(f))
        for run in doc['runs']:
            ctl['sendInputCalls'] = sorted({run['sendInputCalls']})
            ctl['keys'] = sorted({run['keys']})
            for group, pre in (('raw_us', 'raw'), ('burst_hot_us', 'burst'),
                               ('wake_pay_us', 'wake'), ('pipeline_us', 'pipe')):
                for key, value in run[group].items():
                    if key != 'n':
                        metrics.setdefault('%s_%s' % (pre, key), []).append(value)
            metrics.setdefault('rss_mb', []).append(run['peak_rss_mb_total'])
            metrics.setdefault('throughput', []).append(run['throughput_keys_per_s'])
            metrics.setdefault('spike_pct', []).append(
                100.0 * run['lag_spikes_keys'] / run['keys'])
    return metrics, ctl


def micro(d):
    found = {}
    for f in sorted(glob.glob(os.path.join(out, 'bench_engine_%s_r*.txt' % d))):
        for line in open(f):
            m = re.match(r'\s*(.+?)\s+(\d+) ns/key', line)
            if m:
                found.setdefault(m.group(1).strip(), []).append(int(m.group(2)))
    return found


print('== apple-to-apple A/B: %s (baseline) vs %s (candidate) ==' % (base_tag, 'current tree'))
a3, b3 = three('base'), three('head')
if a3 and b3:
    print('\n--- three-engine differential (identical harness, 2M keys/round) ---')
    for i, name in enumerate(['mean', 'p50', 'p90', 'p99', 'max']):
        print('%-5s: baseline=%10.1f  candidate=%10.1f  delta=%+6.2f%%'
              % (name, a3[i], b3[i], 100.0 * (b3[i] - a3[i]) / a3[i]))
    print('keys per run: %s' % sorted({int(x) for x in (a3[5:] + b3[5:])}))
ma, ca = e2e('base')
mb, cb = e2e('head')
if ma and mb:
    print('\n--- e2e_bench (100k keys, burst model) ---')
    print('%-16s %10s %10s %8s' % ('metric', 'baseline', 'candidate', 'delta'))
    for key in ['raw_p50', 'raw_p95', 'raw_p99', 'raw_p999', 'burst_p50', 'burst_p95',
                'burst_p99', 'wake_p50', 'wake_p99', 'pipe_p50', 'pipe_p99',
                'spike_pct', 'throughput', 'rss_mb']:
        if key in ma and key in mb:
            x, y = med(ma[key]), med(mb[key])
            print('%-16s %10.3f %10.3f %+7.2f%%' % (key, x, y, 100.0 * (y - x) / x))
    print('sendInputCalls: baseline=%s candidate=%s | keys=%s | runs/file=%s'
          % (ca.get('sendInputCalls'), cb.get('sendInputCalls'),
             ca.get('keys'), len(glob.glob(os.path.join(out, 'e2e_base_r*.json')))))
xa, xb = micro('base'), micro('head')
if xa:
    print('\n--- TextEngine microbench (ns/key, medians) ---')
    for name in xa:
        x, y = med(xa[name]), med(xb.get(name, [0]))
        print('  %-30s baseline=%4.0f candidate=%4.0f delta=%+6.1f%%'
              % (name, x, y, 100.0 * (y - x) / x))
print('\nraw evidence: %s' % out)
PY
