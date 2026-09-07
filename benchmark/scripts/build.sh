#!/usr/bin/env bash
#==============================================================================
# benchmark/scripts/build.sh — build the 4-way (+A/A control) IME benchmark.
#
#   KieeKey        : src/core/TextEngine.cpp from THIS working tree (unmodified)
#   OpenKey 2.0.5  : tests/reference/openkey-2.0.5 (pristine upstream release)
#   OpenKey latest : benchmark/reference/openkey-master (pristine upstream master)
#   UniKey         : tests/reference/unikey (pristine upstream UKEngine)
#
# Same standard, same -O level, same warning flags for every engine — no
# engine gets a private tuning. The two OpenKey builds are packaged as
# separate shared objects driven by one identical shim, because both define
# the same global engine state; see harness/ok_shim.cpp.
#
#   ./build.sh                 release build  -> benchmark/.build/{bench,bench_mem}
#   ./build.sh --sanitizers    also builds bench_san (ASan+UBSan+LSan)
#   ./build.sh --clean         remove the build dir first
#==============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BUILD="${BENCH_BUILD:-benchmark/.build}"
CXX="${CXX:-g++}"
STD="${BENCH_STD:--std=c++17}"
OPT="${BENCH_OPT:--O2}"
WARN="-w"
WITH_SAN=0
for arg in "$@"; do
    case "$arg" in
        --sanitizers) WITH_SAN=1 ;;
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

build_variant() {
    local sfx="$1" extra="$2"
    local od="$BUILD/obj${sfx}"
    mkdir -p "$od"
    local common="$STD $OPT $WARN $extra"

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

    log "[$sfx] harness"
    $CXX $common -I src/core -I "$HK" -I "$REFUK" -c "$HK/bench.cpp" -o "$od/bench.o"
    local objs=("$od/bench.o" "$od/kk_TextEngine.o" "$od"/uk_*.o)
    if [ -z "$sfx" ]; then
        $CXX $common "${objs[@]}" -ldl -o "$BUILD/bench"
        $CXX $STD $OPT $WARN -DBENCH_ALLOC_TRACK -I src/core -I "$HK" -I "$REFUK" \
             -c "$HK/bench.cpp" -o "$od/bench_mem.o"
        $CXX $common "$od/bench_mem.o" "$od/kk_TextEngine.o" "$od"/uk_*.o -ldl -o "$BUILD/bench_mem"
        log "[$sfx] built $BUILD/bench, $BUILD/bench_mem, libok205.so, libokmaster.so"
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

log "done"
