#!/usr/bin/env bash
#==============================================================================
# benchmark/scripts/campaign_rc1.sh — one measurement campaign, end to end.
#
#   ./campaign_rc1.sh --name=rc1-130 --sessions=8 --rounds=24 \
#                     --engines=contest,attrib --candidate
#
# Everything a campaign consists of lives here, in a fixed order, and every step
# writes its own raw artifact under benchmark/results/<name>/raw/ — no step's
# output is ever merged into another's. The campaign ABORTS on the first failed
# gate (correctness is not a number to be traded against speed here).
#
# Noise controls, in the order they are applied:
#   * the host must be quiet before a single measurement is taken (LOAD_LIMIT);
#   * all engines are timed in the same process, the same round, in a rotated
#     order (paired design — campaign-to-campaign drift cannot enter a paired Δ);
#   * --pin sets CPU affinity for the whole campaign (taskset), so a session does
#     not migrate between a P-core and an E-core mid-round;
#   * round 0 of every session is a warm-up and is dropped by the statistics;
#   * the A/A control (kieekey vs kieekey-aa, two instances of the SAME engine)
#     publishes the instrument's own floor next to every effect;
#   * a second tput pass with --rotate=0 --order-offset alternates the FIXED
#     order, which is how a positional ("who goes first") effect is separated
#     from an engine difference.
#==============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

NAME=""
SESSIONS=6
ROUNDS=20
KEYS=200000
WORDS=74000
L2ROUNDS=10
DIFFAB_SEEDS=3
# The differential's job is per-key transcript equality over LONG interleaved
# streams; word-level identity across the whole corpus is the digest-identity
# gate's job (it runs at full --words). Sizing diffab from the campaign's --words
# gave each of its 18 rows 74 000 extra one-word cases and made a single seed cost
# an hour, so it carries its own smaller corpus: the events compared stay in the
# millions and the step fits inside a campaign instead of outliving it.
DIFFAB_KEYS="${DIFFAB_KEYS:-60000}"
DIFFAB_WORDS="${DIFFAB_WORDS:-4000}"
STEPS="gate,selftest,timer,tput,tput-lead,latency,correctness,diffab,mem,robust,sanitizers,cold,profile,summarize"
ENGINES="contest,ctl,attrib"
PIN="${BENCH_PIN:-auto}"
LOAD_LIMIT="${LOAD_LIMIT:-2.5}"
CANDIDATE=0
DIVERGENCE=0
SAN_WORDS="${SAN_WORDS:-20000}"
SAN_ENGINES="${SAN_ENGINES:-kieekey kieekey-aa kieekey-base kieekey-cand unikey-4.x openkey-2.0.5 openkey-master}"
BUILD="benchmark/.build"
for arg in "$@"; do
    case "$arg" in
        --name=*) NAME="${arg#*=}" ;;
        --sessions=*) SESSIONS="${arg#*=}" ;;
        --rounds=*) ROUNDS="${arg#*=}" ;;
        --keys=*) KEYS="${arg#*=}" ;;
        --words=*) WORDS="${arg#*=}" ;;
        --l2rounds=*) L2ROUNDS="${arg#*=}" ;;
        --diffab-seeds=*) DIFFAB_SEEDS="${arg#*=}" ;;
        --steps=*) STEPS="${arg#*=}" ;;
        --engines=*) ENGINES="${arg#*=}" ;;
        --pin=*) PIN="${arg#*=}" ;;
        --candidate) CANDIDATE=1 ;;
        --divergence) DIVERGENCE=1 ;;   # declared rule trade-off: gate policy, not a gate removal
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done
[ -n "$NAME" ] || { echo "[rc1] --name is required" >&2; exit 2; }

RES="benchmark/results/$NAME"
RAW="$RES/raw"
LOGS="$RES/logs"
mkdir -p "$RAW" "$LOGS"
# gates.txt is APPENDED to, never truncated: a campaign resumed with --steps=…
# (after an interruption) must keep the gate lines its earlier steps already
# established, otherwise the report loses the manifest / correctness evidence for
# a run that is otherwise still valid. Delete the file by hand to start a record
# from scratch.
touch "$LOGS/gates.txt"
GATE_ARGS=(--res="$RES")
[ "$CANDIDATE" = 1 ] && GATE_ARGS+=(--candidate)
if [ "$DIVERGENCE" = 1 ]; then
    # The tree deliberately trades a documented rule away (v1.3.0 RC1's grammarRepair default), so
    # "indistinguishable from frozen v1.2.2" is no longer the property under test. The policy keeps
    # the two that are: final visible text identical everywhere, and this tree's two builds identical
    # to each other. It is a stricter reading of "we know exactly what changed", not a loosened one.
    GATE_ARGS+=(--policy=declared-divergence)
    say "GATE POLICY: declared-divergence — payload differences are measured and published, " \
        "visible text and in-process/cand identity are still asserted"
fi
case ",$ENGINES," in *,attrib,*)
    if [ "$DIVERGENCE" = 1 ]; then
        GATE_ARGS+=(--oracle-engine=kieekey-cand)
    else
        GATE_ARGS+=(--oracle-engine=kieekey-base)
    fi ;;
esac

have_step() { [[ ",$STEPS," == *",$1,"* ]]; }
say() { printf '[rc1] %s\n' "$*"; }
# CPU affinity. "auto" picks the LAST core this container actually exposes —
# pinning to a core that does not exist (a container with fewer CPUs than the
# host) fails in taskset, and silently skipping the pin would let the campaign
# claim isolation it did not have, so the choice is resolved and recorded here.
PINNED=()
if [ "$PIN" = "auto" ]; then
    PIN="$(python3 -c 'import os; a=sorted(os.sched_getaffinity(0)); print(a[-1] if len(a) > 1 else "none")')"
fi
if [ "$PIN" != "none" ] && command -v taskset >/dev/null 2>&1 && taskset -c "$PIN" true 2>/dev/null; then
    PINNED=(taskset -c "$PIN")
    PIN_NOTE="pinned to core $PIN (affinity mask exposes $(python3 -c 'import os;print(len(os.sched_getaffinity(0)))') cores)"
elif [ "$PIN" != "none" ]; then
    PIN_NOTE="NOT AVAILABLE: taskset could not pin to core $PIN — numbers carry scheduler migration noise"
    PIN="none"
else
    PIN_NOTE="disabled by --pin=none"
fi
say "cpu affinity: $PIN_NOTE"

# --- environment record, written BEFORE anything is measured ------------------
# A partial re-run must not overwrite the provenance record of the campaign that
# produced the rows: it did, once, and the report header then announced "6 sessions
# x 20 paired rounds" for a campaign that ran 5 x 12, because the re-run's own defaults
# landed in the file. Fresh campaigns (the ones that run `gate`, and therefore build)
# still own environment.txt; anything else lands in a timestamped side file.
ENVFILE="$RES/environment.txt"
if [ -s "$ENVFILE" ] && ! have_step gate; then
    ENVFILE="$RES/environment.$(date -u +%Y%m%dT%H%M%SZ).patch.txt"
    say "partial re-run: keeping the campaign's environment.txt, this run's facts go to $(basename "$ENVFILE")"
fi
{
    echo "campaign=$NAME"
    echo "utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "sessions=$SESSIONS rounds=$ROUNDS keys=$KEYS words=$WORDS l2rounds=$L2ROUNDS"
    echo "engines=$ENGINES pin=$PIN pin_note=$PIN_NOTE steps=$STEPS candidate=$CANDIDATE"
    echo "head=$(git rev-parse --short HEAD)"
    echo "dirty_src=$(git status --porcelain -- src VERSION CMakeLists.txt | tr '\n' ' ')"
    echo "loadavg1=$(cut -d' ' -f1 /proc/loadavg)"
    uname -a
    cat /proc/loadavg
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
    echo "cores_online=$(grep -c ^processor /proc/cpuinfo)"
    echo "governor=$(cat /sys/devices/system/cpu/cpu$PIN/cpufreq/scaling_governor 2>/dev/null || echo 'NOT AVAILABLE')"
    echo "thp=$(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo 'NOT AVAILABLE')"
    echo "mitigations=$(cat /sys/devices/system/cpu/vulnerabilities/meltdown 2>/dev/null || echo 'NOT AVAILABLE')"
    echo "perf=$(perf --version 2>/dev/null || echo 'NOT AVAILABLE (seccomp blocks perf_event_open in this container)')"
    echo "valgrind=$(command -v valgrind >/dev/null && valgrind --version || echo 'NOT AVAILABLE')"
} > "$ENVFILE" 2>&1
sha256sum src/core/*.hpp src/core/*.cpp > "$LOGS/engine_hashes.txt" 2>&1
cp benchmark/reference/kieekey-1.2.2/UPSTREAM-SHA256.txt "$LOGS/baseline_reference_hashes.txt" 2>/dev/null

# --- the load guard ----------------------------------------------------------
say "waiting for a quiet host (load1 <= $LOAD_LIMIT)"
for _ in $(seq 1 60); do
    load=$(cut -d' ' -f1 /proc/loadavg)
    if awk -v l="$load" -v m="$LOAD_LIMIT" 'BEGIN{exit !(l+0<=m+0)}'; then break; fi
    sleep 2
done
load=$(cut -d' ' -f1 /proc/loadavg)
if ! awk -v l="$load" -v m="$LOAD_LIMIT" 'BEGIN{exit !(l+0<=m+0)}'; then
    echo "[rc1] ABORT: host still at load1=$load after 120 s (limit $LOAD_LIMIT). Numbers taken now would be noise with a narrative attached." >&2
    exit 4
fi
say "host is quiet (load1=$load)"

run() {  # run <label> <cmd…>  — child output to logs/<label>.log
    local label="$1"; shift
    say "$label"
    "${PINNED[@]}" "$@" > "$LOGS/$label.log" 2>&1 < /dev/null
    local rc=$?
    if [ $rc -ne 0 ]; then
        echo "[rc1] STEP FAILED: $label (exit $rc) — last lines:" >&2
        tail -6 "$LOGS/$label.log" >&2
        return 1
    fi
    tail -2 "$LOGS/$label.log"
    return 0
}

BENCH="$BUILD/bench"
BENCHMEM="$BUILD/bench_mem"
BENCHSAN="$BUILD/bench_san"
BENCHPROF="$BUILD/bench_prof"
common=(--words="$WORDS" --keys="$KEYS")

if have_step gate; then
    mkdir -p /home/user/.cache/kbench
    if have_step sanitizers; then
        run "build" ./benchmark/scripts/build.sh --sanitizers || exit 1
    else
        run "build" ./benchmark/scripts/build.sh || exit 1
    fi
    python3 benchmark/scripts/baseline_manifest.py || exit 1
    # Snapshot the manifest WITH the campaign: the tree-level copy is overwritten by the
    # next build, and a campaign's numbers must be attributable to the tree that was
    # built for it (this also decides baseline-vs-candidate wording in rc1_stats).
    cp docs/bench/rc1-130/baseline_manifest.json "$RES/manifest_at_build.json" 2>/dev/null \
        || say "WARNING: could not snapshot the manifest into $RES"
    python3 benchmark/scripts/rc1_gates.py manifest "${GATE_ARGS[@]}" || exit 1
    ( cd benchmark/reference/kieekey-1.2.2 && sha256sum -c UPSTREAM-SHA256.txt >/dev/null ) \
        && say "baseline reference integrity: OK ($(ls benchmark/reference/kieekey-1.2.2 | wc -l) files)" \
        || { echo "[rc1] FATAL: baseline reference copy differs from its recorded hashes" >&2; exit 3; }
fi

if have_step selftest; then
    say "selftest (drivers, A/A identity, encoder, attribution guard)"
    : > "$RAW/selftest.jsonl"
    run "selftest" "$BENCH" --mode=selftest || exit 1
    case ",$ENGINES," in
        *,attrib,*)
            run "attrib-guard" "$BENCH" --mode=attrib-guard --keys=8000 \
                --out="$RAW/selftest.jsonl" --append || exit 1
            python3 benchmark/scripts/rc1_gates.py attrib-guard "${GATE_ARGS[@]}" || exit 1 ;;
    esac
fi

have_step timer && { run "timer" "$BENCH" --mode=timer --out="$RAW/timer.jsonl" || exit 1; }

if have_step tput; then
    say "L1 throughput — $SESSIONS sessions x $ROUNDS paired rounds"
    for s in $(seq 0 $((SESSIONS - 1))); do
        run "tput:s$s" "$BENCH" --mode=tput --engines="$ENGINES" --session="s$s" \
            --seed-idx="$s" --rounds="$ROUNDS" --rotate=1 "${common[@]}" \
            --out="$RAW/tput_s$s.jsonl" || exit 1
    done
    say "tput rows: $(cat "$RAW"/tput_s*.jsonl | grep -c '"mode":"tput"')"
fi

if have_step tput-lead; then
    say "L1 fixed-lead ordering test (two passes with opposite fixed order)"
    for s in $(seq 0 $((SESSIONS - 1))); do
        run "tput-lead:s$s" "$BENCH" --mode=tput --engines="$ENGINES" --session="lead$s" \
            --seed-idx="$s" --rounds="$ROUNDS" --rotate=0 --order-offset=$((s % 2)) \
            "${common[@]}" --out="$RAW/tput_lead_s$s.jsonl" || exit 1
    done
fi

have_step latency && {
    run "latency" "$BENCH" --mode=latency --engines="$ENGINES" --rounds="$L2ROUNDS" \
        "${common[@]}" --out="$RAW/latency.jsonl" || exit 1
}

if have_step correctness; then
    run "correctness" "$BENCH" --mode=correctness --engines="$ENGINES" --rounds=1 \
        --words="$WORDS" --out="$RAW/correctness.jsonl" || exit 1
    python3 benchmark/scripts/rc1_gates.py correctness "${GATE_ARGS[@]}" || exit 1
    python3 benchmark/scripts/rc1_gates.py digest-identity "${GATE_ARGS[@]}" || exit 1
fi

if have_step diffab; then
    case ",$ENGINES," in
        *,attrib,*)
            say "differential: frozen v1.2.2 vs current tree ($DIFFAB_SEEDS seeds)"
            : > "$RAW/diffab.jsonl"
            for sd in $(seq 0 $((DIFFAB_SEEDS - 1))); do
                run "diffab:seed$sd" "$BENCH" --mode=diffab --engines=kieekey-cand,kieekey-base \
                    --seed-idx="$sd" --keys="$DIFFAB_KEYS" --words="$DIFFAB_WORDS" \
                    --out="$RAW/diffab.jsonl" --append || exit 1
            done
            # An absolute floor, not one derived from --keys: diffab runs with its
            # own corpus size now, and the protocol's claim is "millions of per-key
            # comparisons", which is exactly what this checks.
            python3 benchmark/scripts/rc1_gates.py diffab "${GATE_ARGS[@]}" \
                --min-events="${DIFFAB_MIN_EVENTS:-1000000}" || exit 1 ;;
        *) say "diffab skipped — the attribution pair (--engines containing 'attrib') is not in this campaign" ;;
    esac
fi

if have_step mem; then
    say "memory (isolated process per engine)"
    rm -f "$RAW"/mem_*.jsonl
    for e in kieekey kieekey-base kieekey-cand unikey-4.x openkey-2.0.5 openkey-master; do
        run "mem:$e" "$BENCHMEM" --mode=mem --engine="$e" --soak=2000000 \
            --words="$WORDS" --out="$RAW/mem_$e.jsonl" >>"$LOGS/mem.log" 2>&1 || true
    done
    python3 benchmark/scripts/rc1_gates.py memory "${GATE_ARGS[@]}" || exit 1
fi

have_step robust && { run "robust" "$BENCH" --mode=robust --engines="$ENGINES" \
    --words="$WORDS" --out="$RAW/robust.jsonl" || exit 1; }

if have_step sanitizers; then
    if [ ! -x "$BENCHSAN" ]; then
        say "sanitizers: bench_san not built (run: BENCH_SAN=1 benchmark/scripts/build.sh --sanitizers) — SKIPPED"
    else
        say "sanitizers per engine (ASan+UBSan+LSan)"
        for e in $SAN_ENGINES; do
            # A sanitizer run is a UB/leak search, not a throughput measurement, so it
            # uses a smaller corpus than the timed passes: ASan costs 2-3x and the
            # point is the report, not the word count. Recorded here so nobody reads
            # it as a narrower correctness claim than it is — the corpus-wide claim is
            # the digest/differential gates, which run at full --words.
            run "san_$e" "$BENCHSAN" --mode=correctness --engine="$e" --rounds=1 \
                --words="$SAN_WORDS" --out="/dev/null" > "$LOGS/san_$e.log" 2>&1 || true
            # an engine that crashes under sanitizers must be visible even when
            # the sanitizer report itself is the finding
            cat "$LOGS/san_$e.log" >> "$LOGS/sanitizers_combined.log"
        done
        python3 benchmark/scripts/rc1_gates.py sanitizers "${GATE_ARGS[@]}" || exit 1
    fi
fi

have_step cold && { run "cold" "$BENCH" --mode=cold --engines="$ENGINES" "${common[@]}" \
    --out="$RAW/cold.jsonl" || exit 1; }

if have_step profile; then
    for cfg in as-shipped matched-minimal; do
        for e in kieekey unikey-4.x; do
            run "profile:$e:$cfg" "$BENCHPROF" --mode=profile --engine="$e" --config="$cfg" \
                --keys="$KEYS" --words="$WORDS" --rounds=8 --hz=1000 \
                --out="$RAW/profile_${e}_${cfg}.jsonl" || exit 1
            python3 benchmark/tools/prof_sym.py --profile="$RAW/profile_${e}_${cfg}.jsonl" \
                --bin="$BUILD/bench_prof" --md="$LOGS/profile_${e}_${cfg}.md" 2>/dev/null \
            || python3 benchmark/scripts/prof_sym.py --profile="$RAW/profile_${e}_${cfg}.jsonl" \
                --bin="$BUILD/bench_prof" --md="$LOGS/profile_${e}_${cfg}.md" || exit 1
        done
    done
fi

if have_step summarize; then
    say "summarising"
    # The artifact-shape gate runs *before* stats on purpose: it reads every raw file,
    # so it can state how many rows the campaign actually wrote, and a torn trailing
    # line from an interrupted step fails the campaign instead of being silently
    # skipped by whichever gate happened to read that file.
    python3 benchmark/scripts/rc1_gates.py integrity --res="$RES" || exit 1
    python3 benchmark/scripts/rc1_stats.py --results="$RES" || exit 1
    # rc1_stats owns the profile append: it writes those blocks into the tables.md it
    # just generated, so a re-summarise cannot duplicate or drop them (both have
    # happened when the shell driver appended them after the fact).
    for need in timer.jsonl selftest.jsonl; do
        if [ ! -s "$RAW/$need" ]; then
            say "WARNING: raw/$need is missing or empty — a step was excluded from this "
            say "         run, and the report will refuse to resolve the tokens that read"
            say "         it (that refusal is the intended failure, not a rendering bug)"
        fi
    done
fi

say "release trail: docs/bench/rc1-130/$NAME"
say "campaign $NAME complete"
