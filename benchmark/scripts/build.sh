#!/usr/bin/env bash
#==============================================================================
# benchmark/scripts/build.sh — build the 4-way (+A/A control) IME benchmark.
#
#   KieeKey        : src/core/TextEngine.cpp from THIS working tree (unmodified)
#   OpenKey 2.0.5  : tests/reference/openkey-2.0.5 (pristine upstream release)
#   OpenKey latest : benchmark/reference/openkey-master (pristine upstream master)
#   UniKey         : tests/reference/unikey (pristine upstream UKEngine)
#
# Same standard, same -O level, same defines, same warning flags for every
# engine — no engine gets a private tuning.
#
# The optimisation defaults mirror the PRODUCT build (CMake Release = -O3
# -DNDEBUG), and that is not a cosmetic choice: TextEngine.cpp keeps its D2
# root-invariant loop inside #ifndef NDEBUG, so a build without -DNDEBUG charges
# KieeKey for debug bookkeeping the shipped binary never runs. BENCH_OPT and
# BENCH_DEF override both, for sensitivity runs; whatever is in force is recorded
# in the campaign's environment.txt and in docs/bench/rc1-130/baseline_manifest.json
# (which reads them back out of this file, so it cannot claim flags this build
# does not use).
#
# The attribution pair (libkkbase.so = frozen v1.2.2, libkkcand.so = this tree)
# is built from ONE shim source with ONE set of flags, so the only difference
# between the two columns is the engine translation unit behind it. The two OpenKey builds are packaged as
# separate shared objects driven by one identical shim, because both define
# the same global engine state; see harness/ok_shim.cpp.
#
#   ./build.sh                 release build  -> benchmark/.build/{bench,bench_mem}
#   ./build.sh --sanitizers    also builds bench_san (ASan+UBSan+LSan)
#   ./build.sh --pgo           also rebuilds libkkcand.so with profile feedback (see below)
#   ./build.sh --clean         remove the build dir first
#==============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BUILD="${BENCH_BUILD:-benchmark/.build}"
CXX="${CXX:-g++}"
STD="${BENCH_STD:--std=c++17}"
OPT="${BENCH_OPT:--O3}"
DEF="${BENCH_DEF:--DNDEBUG}"
WARN="-w"
WITH_SAN=0
WITH_PGO=0
for arg in "$@"; do
    case "$arg" in
        --sanitizers) WITH_SAN=1 ;;
        --pgo) WITH_PGO=1 ;;
        --clean) rm -rf "$BUILD" ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

REF205="tests/reference/openkey-2.0.5/engine"
REFUK="tests/reference/unikey"
REFM="benchmark/reference/openkey-master/engine"
HK="benchmark/harness"
UK_FIX="-include $ROOT/tests/reference/unikey/uk_fix.h"
mkdir -p "$BUILD"

log() { printf '[build] %s\n' "$*"; }

# --- the competitor reference trees must be pristine ------------------------
verify_pristine() {
    local dir="$1" label="$2"
    if [ -f "$dir/UPSTREAM-SHA256.txt" ]; then
        ( cd "$dir" && sha256sum -c UPSTREAM-SHA256.txt >/dev/null ) \
            && log "$label: pristine ($(find "$dir" -type f -not -name 'UPSTREAM-SHA256.txt' | wc -l) files match the recorded hashes)" \
            || { echo "[build] FATAL: $label differs from its recorded upstream hashes" >&2; exit 3; }
    else
        log "$label: no UPSTREAM-SHA256.txt (skipping integrity check)"
    fi
}
verify_pristine "benchmark/reference/openkey-master" "OpenKey latest reference"
# The v1.2.2 baseline copy is what libkkbase.so is built from. If it ever
# differs from its recorded hashes, "identical to baseline" and the whole
# gain-over-v1.2.2 column mean nothing, so the build stops instead.
verify_pristine "benchmark/reference/kieekey-1.2.2" "KieeKey v1.2.2 baseline reference"

build_variant() {
    local sfx="$1" extra="$2"
    local od="$BUILD/obj${sfx}"
    mkdir -p "$od"
    local common="$STD $OPT $DEF $WARN $extra"

    log "[$sfx] KieeKey engine (this working tree)"
    $CXX $common -I src/core -c src/core/TextEngine.cpp -o "$od/kk_TextEngine.o"

    log "[$sfx] UniKey UKEngine (pristine reference)"
    for f in ukengine inputproc macro mactab usrkeymap charset convert data error pattern byteio; do
        $CXX $common -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/$f.cpp" -o "$od/uk_$f.o"
    done

    log "[$sfx] OpenKey 2.0.5 -> libok205.so"
    for f in Engine Vietnamese Macro SmartSwitchKey; do
        $CXX $common -fPIC -DLINUX -I"$REF205" -c "$REF205/$f.cpp" -o "$od/e205_$f.o"
    done
    $CXX $common -fPIC -DLINUX -I"$REF205" -include algorithm -c "$REF205/ConvertTool.cpp" -o "$od/e205_ConvertTool.o"
    $CXX $common -fPIC -DLINUX -I"$REF205" -I tests -c tests/engine205.cpp -o "$od/e205_wrap.o"
    $CXX $common -fPIC -I tests -I "$HK" -c "$HK/ok_shim.cpp" -o "$od/e205_shim.o"
    $CXX $common -shared -o "$BUILD/libok205$sfx.so" "$od"/e205_*.o
    log "[$sfx] OpenKey latest (master) -> libokmaster.so"
    for f in Engine Vietnamese Macro SmartSwitchKey; do
        $CXX $common -fPIC -DLINUX -I"$REFM" -c "$REFM/$f.cpp" -o "$od/m_$f.o"
    done
    $CXX $common -fPIC -DLINUX -I"$REFM" -include algorithm -c "$REFM/ConvertTool.cpp" -o "$od/m_ConvertTool.o"
    # One wrapper source, second instance: the namespace rename gives the
    # latest build its own wrapper symbols. Engine sources stay byte-identical.
    $CXX $common -fPIC -DLINUX -Dok205=okmaster -I"$REFM" -I tests -c tests/engine205.cpp -o "$od/m_wrap.o"
    $CXX $common -fPIC -Dok205=okmaster -I tests -c "$HK/ok_shim.cpp" -o "$od/m_shim.o"
    $CXX $common -shared -o "$BUILD/libokmaster$sfx.so" "$od"/m_*.o

    # ---- the v1.3.0 RC1 attribution pair: same shim, two engine TUs ----
    log "[$sfx] KieeKey attribution pair (frozen v1.2.2 vs this tree)"
    for side in base cand; do
        if [ "$side" = base ]; then
            edir="benchmark/reference/kieekey-1.2.2"
        else
            edir="src/core"
        fi
        $CXX $common -fPIC -fvisibility=hidden -DKK_BUILD_ID=kk_$side -I "$edir" \
            -c "$edir/TextEngine.cpp" -o "$od/kk_${side}_engine.o"
        $CXX $common -fPIC -fvisibility=hidden -DKK_BUILD_ID=kk_$side -I "$edir" \
            -I "$HK" -c "$HK/kk_shim.cpp" -o "$od/kk_${side}_shim.o"
        $CXX $common -shared -o "$BUILD/libkk${side}${sfx}.so" \
            "$od/kk_${side}_engine.o" "$od/kk_${side}_shim.o"
    done

    log "[$sfx] harness"
    $CXX $common -I src/core -I "$HK" -I "$REFUK" -c "$HK/bench.cpp" -o "$od/bench.o"
    local objs=("$od/bench.o" "$od/kk_TextEngine.o" "$od"/uk_*.o)
    if [ -z "$sfx" ]; then
        $CXX $common "${objs[@]}" -ldl -o "$BUILD/bench"
        $CXX $STD $OPT $WARN -DBENCH_ALLOC_TRACK -I src/core -I "$HK" -I "$REFUK" \
             -c "$HK/bench.cpp" -o "$od/bench_mem.o"
        $CXX $common "$od/bench_mem.o" "$od/kk_TextEngine.o" "$od"/uk_*.o -ldl -o "$BUILD/bench_mem"
        # Debug-info twin of the engine object, used ONLY by bench_prof so the
        # SIGPROF samples can be attributed to source lines inside the engine.
        # Same flags plus -g: the profile ranks, and every decision is made on
        # the plain binary above.
        # -no-pie/-fno-pie on the profiling twin only: the profile artifact stores
        # raw program counters and prof_sym.py resolves them against `nm` addresses,
        # which is valid only when the image is not relocated by ASLR. Built as a PIE
        # the entire run resolved to "??" (100 % of samples) — and an unreadable
        # profile looks exactly like "no hot spot", so this flag is load-bearing.
        $CXX $common -g -fno-omit-frame-pointer -fno-pie -I src/core -c src/core/TextEngine.cpp \
            -o "$od/kk_TextEngine_prof.o"
        $CXX $common -g -fno-omit-frame-pointer -fno-pie -I src/core -I "$HK" -I "$REFUK" \
            -c "$HK/bench.cpp" -o "$od/bench_prof.o"
        $CXX $common -g -fno-omit-frame-pointer -no-pie "$od/bench_prof.o" \
            "$od/kk_TextEngine_prof.o" "$od"/uk_*.o -ldl -o "$BUILD/bench_prof"
        log "[$sfx] built $BUILD/bench, $BUILD/bench_mem, $BUILD/bench_prof, libok205.so, libokmaster.so, libkkbase.so, libkkcand.so"
    else
        $CXX $common "${objs[@]}" -ldl -o "$BUILD/bench$sfx"
        log "[$sfx] built $BUILD/bench$sfx"
    fi
}

build_variant "" ""

if [ "$WITH_SAN" = "1" ]; then
    log "sanitizer build (ASan + UBSan + LSan) — separate objects, same sources"
    build_variant "_san" "-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=address,undefined -g"
fi

if [ "$WITH_PGO" = "1" ]; then
    # ---- profile-guided variant of the CANDIDATE library only ----------------
    # The measurement keeps its shape on purpose: libkkbase.so stays a plain
    # -O3 build of the frozen sources, libkkcand.so becomes the same tree built
    # with profile feedback, so the campaign's paired gain table *is* the PGO
    # effect -- same rounds, same A/A band, same order control, no cross-campaign
    # arithmetic. Two rules keep it from being self-flattery:
    #   * training reads a corpus window disjoint from the measured one
    #     (--seed-idx rotates the window; the campaign measures seeds 0..2, so
    #     training at 7 never sees an evaluation stream), and
    #   * training runs the engine's own workload (tput across both
    #     configurations, both methods and all three streams, plus the
    #     correctness and robust streams) rather than a chosen microbenchmark.
    # Only the engine TU is PGO'd; the shim stays plain, which is the
    # conservative direction (any glue cost is left on the table).
    log "pgo: instrumented training build (engine TU only)"
    POD="$BUILD/obj_pgo"
    rm -rf "$POD"; mkdir -p "$POD/prof"
    PGFLAG="-fprofile-generate=$POD/prof"
    $CXX $STD $OPT $DEF $WARN $PGFLAG -fPIC -fvisibility=hidden \
        -DKK_BUILD_ID=kk_cand -I src/core -c src/core/TextEngine.cpp \
        -o "$POD/kk_cand_engine.o"
    $CXX $STD $OPT $DEF $WARN -o "$BUILD/bench_pgo_train" "$BUILD/obj/bench.o" \
        "$POD/kk_cand_engine.o" "$BUILD"/obj/uk_*.o $PGFLAG -ldl
    # The passes mirror what the campaign measures, so the profile is the engine's
    # real branch mix rather than one convenient loop. --mode=tput already covers
    # both configurations x both methods x all three streams in a single run.
    log "pgo: training (seed-idx 7 — a window the campaign never measures)"
    pgo_pass() {
        "$BUILD/bench_pgo_train" "$@" --out="$POD/train.jsonl" --append >/dev/null || {
            echo "[build] FATAL: PGO training pass failed: $*" >&2
            exit 5
        }
    }
    pgo_pass --mode=tput --engines=kieekey --session=train --seed-idx=7 \
        --rounds=8 --rotate=1 --words=74000 --keys=150000
    pgo_pass --mode=latency --engines=kieekey --rounds=2 --words=74000 --keys=40000
    pgo_pass --mode=correctness --engines=kieekey --rounds=1 --words=74000
    pgo_pass --mode=robust --engines=kieekey --rounds=1
    n_gcda=$(find "$POD/prof" -name '*.gcda' -size +0 | wc -l)
    if [ "$n_gcda" -lt 1 ]; then
        echo "[build] FATAL: no .gcda written — a '-fprofile-use' build would silently" >&2
        echo "         fall back to plain -O3 and the comparison would be fiction" >&2
        exit 5
    fi
    log "pgo: $n_gcda profile file(s); rebuilding the candidate TU with -fprofile-use"
    $CXX $STD $OPT $DEF $WARN -fprofile-use="$POD/prof" -Werror=coverage-mismatch \
        -fPIC -fvisibility=hidden -DKK_BUILD_ID=kk_cand -I src/core \
        -c src/core/TextEngine.cpp -o "$POD/kk_cand_engine_pgo.o"
    $CXX $STD $OPT $DEF $WARN -shared -o "$BUILD/libkkcand.so" \
        "$POD/kk_cand_engine_pgo.o" "$BUILD/obj/kk_cand_shim.o"
    ( cd "$POD/prof" && find . -name '*.gcda' -print0 | sort -z | xargs -0 sha256sum ) \
        > "$POD/profile-sha256.txt" 2>/dev/null || true
    log "pgo: libkkcand.so is now the PGO build; profile digests in $POD/profile-sha256.txt"
    log "pgo: NOTE libkkbase.so and bench remain plain -O3 by design"
fi

log "done"
