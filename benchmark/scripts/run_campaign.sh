#!/usr/bin/env bash
#==============================================================================
# benchmark/scripts/run_campaign.sh — the noise-minimising measurement campaign.
#
# Protocol (fixed BEFORE measuring; see benchmark/README.md "Protocol"):
#   * every run is pinned to ONE cpu core with taskset, one process per mode
#   * each engine is warmed up (full stream, discarded) before it is timed
#   * latency rounds are INTERLEAVED across engines with a rotating start
#     index, so no engine is always first (cold cache) or always last
#   * N rounds per stream; every round is written out raw (nothing averaged in
#     the harness), statistics are computed by scripts/summarize.py
#   * an A/A control (a second instance of the KieeKey engine, same binary) and
#     a repeated campaign define the noise band; the report never calls a
#     difference real unless it clears that band
#   * competitor reference trees are hash-verified pristine before running
#
# Usage:  run_campaign.sh [--campaign=ID] [--rounds=N] [--words=N] [--keys=N]
#                         [--skip-sanitizers] [--cores=LIST]
#==============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

CID="${CID:-campaign-a}"
ROUNDS="${ROUNDS:-7}"
WORDS="${WORDS:-74000}"
KEYS="${KEYS:-200000}"   # keys per latency stream (matches the harness default)
CORES="${CORES:-}"
SKIP_SAN=0
STEPS="encoder,selftest,correctness,latency,mem,overhead,robust,sanitizers"
has_step() { [[ ",$STEPS," == *",$1,"* ]]; }
for arg in "$@"; do
    case "$arg" in
        --campaign=*) CID="${arg#*=}" ;;
        --rounds=*) ROUNDS="${arg#*=}" ;;
        --words=*) WORDS="${arg#*=}" ;;
        --keys=*) KEYS="${arg#*=}" ;;
        --cores=*) CORES="${arg#*=}" ;;
        --skip-sanitizers) SKIP_SAN=1 ;;
        --steps=*) STEPS="${arg#*=}" ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

OUT="benchmark/results/$CID"
BUILD="benchmark/.build"
mkdir -p "$OUT/raw" "$OUT/logs"

# ---------------------------------------------------------------------------
# host freeze
# ---------------------------------------------------------------------------
if [ -z "$CORES" ]; then
    CORES=$(nproc)
    CORES=$((CORES > 1 ? CORES - 1 : 0))
fi
PIN="taskset -c $CORES"
command -v taskset >/dev/null || { echo "[run] taskset missing — running unpinned"; PIN=""; }

{
    echo "campaign=$CID"
    echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host=$(uname -n) kernel=$(uname -r) os=$(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME")"
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "cores_total=$(nproc) pinned_core=$CORES"
    echo "cpuinfo_mhz=$(awk -F: '/cpu MHz/{gsub(/ /,"",$2); print $2; exit}' /proc/cpuinfo)"
    echo "governor=$(cat /sys/devices/system/cpu/cpu$CORES/cpufreq/scaling_governor 2>/dev/null || echo 'n/a (vm)')"
    echo "turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo 'n/a')"
    echo "aslr=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null || echo n/a)"
    echo "compiler=$(${CXX:-g++} --version 2>/dev/null | head -1)"
    echo "flags: BENCH_STD=${BENCH_STD:--std=c++17} BENCH_OPT=${BENCH_OPT:--O2} (identical for every engine)"
    echo "loadavg_start=$(cut -d' ' -f1-3 /proc/loadavg)"
    echo "nprocs=$(ps -e --no-headers | wc -l)"
    echo "bogo_mips=$(grep -m1 '^BogoMIPS' /proc/cpuinfo | cut -d: -f2- | tr -d ' ')"
    echo "cgroup_cpu_max=$(cat /sys/fs/cgroup/cpu.max 2>/dev/null || echo n/a)"
    echo "cgroup_mem_max=$(cat /sys/fs/cgroup/memory.max 2>/dev/null || echo n/a)"
    echo "vmstat_throttle_before=$(awk '/nr_throttled|throttled_time/{printf "%s=%s ", $1, $2}' /proc/self/stat 2>/dev/null || echo n/a)"
} > "$OUT/environment.txt" 2>&1
cat "$OUT/environment.txt"

export BENCH_LIB_OK205="$BUILD/libok205.so"
export BENCH_LIB_OKMASTER="$BUILD/libokmaster.so"

run() {  # run <label> <cmd...>
    local label="$1"; shift
    echo "[run] $label"
    local t0 t1
    t0=$(date +%s)
    # shellcheck disable=SC2086
    $PIN "$@" > "$OUT/raw/$label.jsonl" 2> "$OUT/logs/$label.log"
    t1=$(date +%s)
    echo "  lines=$(wc -l < "$OUT/raw/$label.jsonl") secs=$((t1 - t0))"
    echo "$label secs=$((t1 - t0)) rc=0" >> "$OUT/raw/_manifest.txt"
}

: > "$OUT/raw/_manifest.txt"

# ---------------------------------------------------------------------------
# 1. integrity + wiring
# ---------------------------------------------------------------------------
( cd benchmark/reference/openkey-master && sha256sum -c UPSTREAM-SHA256.txt ) \
    > "$OUT/logs/reference_integrity.txt" 2>&1
sha256sum tests/reference/openkey-2.0.5/engine/*.cpp tests/reference/openkey-2.0.5/engine/*.h \
          tests/reference/unikey/*.cpp tests/reference/unikey/*.h \
          benchmark/reference/openkey-master/engine/*.cpp benchmark/reference/openkey-master/engine/*.h \
          src/core/TextEngine.cpp src/core/TextEngine.hpp > "$OUT/logs/input_hashes.txt"
if has_step selftest; then
# 1b. the key model itself is cross-checked against an independent Python
#     re-implementation before any engine is measured (a wrong encoder would
#     bias every engine the same way, so it must not be trusted to be right).
if has_step encoder; then
    echo "[run] encoder cross-check"
    "$BUILD/bench" --mode=encoder-dump --words=3000 --out="$OUT/raw/encoder_dump.tsv" \
        > "$OUT/logs/encoder_dump.log" 2>&1
    python3 benchmark/scripts/check_encoder.py --dump="$OUT/raw/encoder_dump.tsv" \
        | tee "$OUT/logs/encoder_check.txt"
    grep -q "OK —" "$OUT/logs/encoder_check.txt" || { echo "[run] ENCODER CHECK FAILED"; exit 1; }
fi

run selftest "$BUILD/bench" --mode=selftest --words="$WORDS" --keys="$KEYS"
fi

# ---------------------------------------------------------------------------
# 2. correctness (as-shipped + feature-matched, 3 encodings) + invariants
# ---------------------------------------------------------------------------
if has_step correctness; then
run correctness "$BUILD/bench" --mode=correctness --words="$WORDS" --seed=20260907
fi

# ---------------------------------------------------------------------------
# 3. latency: interleaved rounds, rotating order
# ---------------------------------------------------------------------------
if has_step latency; then
run latency "$BUILD/bench" --mode=latency --rounds="$ROUNDS" --keys="$KEYS" --words=20000
fi

# ---------------------------------------------------------------------------
# 4. memory (allocation-tracking build)
# ---------------------------------------------------------------------------
if has_step mem; then
    # one process per engine: RSS measured in a shared process attributes
    # first-touch pages to whichever engine ran before it.
    for eng in kieekey kieekey-aa openkey-2.0.5 openkey-master unikey-4.x; do
        run "mem-$eng" "$BUILD/bench_mem" --mode=mem --soak=2000000 --engine="$eng"
    done
fi

# ---------------------------------------------------------------------------
# 5. robustness: hostile streams, one child process per engine
# ---------------------------------------------------------------------------
if has_step overhead; then
run overhead "$BUILD/bench" --mode=overhead
fi

if has_step robust; then
run robust "$BUILD/bench" --mode=robust
fi

# ---------------------------------------------------------------------------
# 6. sanitizers: same streams per engine, isolated per engine so one engine's
#    report cannot mask another's
# ---------------------------------------------------------------------------
if [ "$SKIP_SAN" = "0" ] && has_step sanitizers && [ -x "$BUILD/bench_san" ]; then
    # bench_san must load the *instrumented* OpenKey objects, or the OpenKey
    # columns would be un-instrumented inside a sanitizer run.
    export BENCH_LIB_OK205="$BUILD/libok205_san.so"
    export BENCH_LIB_OKMASTER="$BUILD/libokmaster_san.so"
    for eng in kieekey openkey-2.0.5 openkey-master unikey-4.x; do
        echo "[run] san:$eng"
        # shellcheck disable=SC2086
        ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:halt_on_error=0 \
        UBSAN_OPTIONS=print_stacktrace=1 \
        $PIN "$BUILD/bench_san" --mode=robust --engine="$eng" \
            > "$OUT/raw/san_$eng.jsonl" 2> "$OUT/logs/san_$eng.log"
        echo "  sanitizer log lines=$(wc -l < "$OUT/logs/san_$eng.log")"
    done
    for eng in kieekey openkey-2.0.5 openkey-master unikey-4.x; do
        echo "[run] san-correctness:$eng"
        ASAN_OPTIONS=detect_leaks=0:abort_on_error=0:halt_on_error=0 \
        $PIN "$BUILD/bench_san" --mode=correctness --words="${SAN_WORDS:-800}" \
            --engine="$eng" \
            > "$OUT/raw/sanfix_$eng.jsonl" 2> "$OUT/logs/sanfix_$eng.log" || true
    done
    export BENCH_LIB_OK205="$BUILD/libok205.so"
    export BENCH_LIB_OKMASTER="$BUILD/libokmaster.so"
fi

echo "loadavg_end=$(cut -d' ' -f1-3 /proc/loadavg)" >> "$OUT/environment.txt"
python3 benchmark/scripts/summarize.py --results="$OUT" --campaign="$CID" || \
    echo "[run] summarize.py not finished yet — raw artifacts are in $OUT"
echo "[run] campaign $CID complete: $OUT"
