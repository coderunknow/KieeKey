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
#   KieeKey v1.2.0 Stable - refactored and completed logic
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
# File: tests/run_all_tests.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#----------------------------------------------------------------------------
# tests/run_all_tests.sh — build and run the WHOLE native (non-Windows) test
# suite with one command.
#
# WHY THIS EXISTS (v1.2.0 Stable, release-gate requirement):
#   Before this script, the repository carried fifteen test programs but only
#   THREE of them (ok_tests, ok_ring_tests, ok_wrap_tests) were registered in
#   CMakeLists.txt — i.e. `ctest` exercised a quarter of the suite and the
#   stress/soak/differential harnesses were run by hand, if at all. A test
#   that is never run cannot protect a release. This runner builds every
#   native harness deterministically, runs each under a hard timeout, and
#   reports a single pass/fail verdict.
#
# Layers:
#   1. unit        — golden vectors + component tests (engine, ring, wrapper,
#                    output items, hotfixes, v3.3.1 features)
#   2. stress      — fuzz + invariants + determinism + allocation counting
#   3. soak        — long-running mixed workload, flat-allocation/RSS check
#   4. differential— clean-room oracle + the vendored 2.0.5 engine
#                    (gate_correctness, edge_behaviors, dirty_input,
#                     real_passages)
#
# Usage:
#   tests/run_all_tests.sh [--cxx=g++] [--out=DIR] [--jobs=N] [--quick]
#
#   --quick   reduced iteration counts (local iteration; NOT the release gate)
#
# Exit: 0 = every layer passed, 1 = at least one failure.
#----------------------------------------------------------------------------
set -u
set -o pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-g++}"
OUT="${REPO_ROOT}/build/test-run"
JOBS=2
QUICK=0

for arg in "$@"; do
    case "$arg" in
        --cxx=*)  CXX="${arg#--cxx=*}" ;;
        --out=*)  OUT="${arg#--out=*}" ;;
        --jobs=*) JOBS="${arg#--jobs=*}" ;;
        --quick)  QUICK=1 ;;
        *) echo "run_all_tests.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done

mkdir -p "$OUT/bin" "$OUT/logs"
BUILD_LOG="$OUT/logs/build.log"
: > "$BUILD_LOG"

# Standard the project compiles with: C++23 where the harness needs it,
# C++17 for the differential harnesses (they link the vendored C++11-era
# 2.0.5 sources and must keep their historic dialect).
OK205_SRC="tests/engine205.cpp"
REF_SRC="tests/reference/openkey-2.0.5/engine/Engine.cpp
         tests/reference/openkey-2.0.5/engine/Vietnamese.cpp
         tests/reference/openkey-2.0.5/engine/Macro.cpp
         tests/reference/openkey-2.0.5/engine/SmartSwitchKey.cpp"

ENGINE="src/core/TextEngine.cpp"   # legacy alias; targets link $ENGINE23/$ENGINE17
INC="-Isrc/core -Itests"

say()  { printf '%s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; }

# Quote a value so it survives being embedded in a generated script.
q() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\''/g")"; }

# Build jobs are RECORDED, not executed, then dispatched with xargs -P "$JOBS".
# (Before v1.2.0 Stable the builds ran strictly sequentially and the --jobs
# flag was parsed but never used, so a 16-target run serialised ~45 s of
# compiler work even though every target is independent.)
JOBS_DIR="$OUT/jobs"
mkdir -p "$JOBS_DIR"
BJOBS=()
build() { # $1 = target name, rest = sources+flags
    local name="$1"; shift
    BJOBS+=("$name")
    {
        printf '#!/bin/bash\nset -u\ncd %s\n' "$(q "$REPO_ROOT")"
        printf 'if %s ' "$(q "$CXX")"
        local a
        for a in "$@"; do printf '%s ' "$(q "$a")"; done
        printf -- '-o %s >> %s 2>&1\n' "$(q "$OUT/bin/$name")" "$(q "$BUILD_LOG")"
        printf 'then printf 0 > %s\nelse printf 1 > %s\nfi\n' \
            "$(q "$JOBS_DIR/$name.rc")" "$(q "$JOBS_DIR/$name.rc")"
    } > "$JOBS_DIR/$name.sh"
}

dispatch_builds() {
    local n
    printf '%s\n' "${BJOBS[@]}" | xargs -P "$JOBS" -I{} bash "$JOBS_DIR/{}.sh"
    for n in "${BJOBS[@]}"; do
        printf '  [build] %-22s' "$n"
        if [ "$(cat "$JOBS_DIR/$n.rc" 2>/dev/null)" = "0" ]; then
            printf ' ok\n'
        else
            printf ' FAILED (see %s)\n' "$BUILD_LOG"
            rc=1
        fi
    done
}

say "== KieeKey native test suite =="
say "   compiler : $CXX ($("$CXX" --version 2>/dev/null | head -1))"
say "   output   : $OUT"
say "   quick    : $QUICK"
say ""

say "-- building --------------------------------------------------------"
rc=0

# src/core/TextEngine.cpp is by far the dominant translation unit (~1.7 s of
# the ~2.5 s each target costs) and every engine-linked target used to compile
# it again -- 13 times, ~22 s of pure waste. Compile it ONCE per language
# dialect and link the object. The two dialects (C++23 for the modern
# harnesses, C++17 for the differential harnesses that also link the vendored
# C++11-era OpenKey 2.0.5 sources) are exactly the pair the script already
# used, so every target links the same code it always did.
OBJ_DIR="$OUT/obj"
mkdir -p "$OBJ_DIR"
"$CXX" -std=c++2b -O2 $INC -c src/core/TextEngine.cpp -o "$OBJ_DIR/te23.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "TextEngine.cpp (C++23) failed to compile"; exit 1; }
"$CXX" -std=c++17 -O2 $INC -c src/core/TextEngine.cpp -o "$OBJ_DIR/te17.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "TextEngine.cpp (C++17) failed to compile"; exit 1; }
ENGINE23="$OBJ_DIR/te23.o"
ENGINE17="$OBJ_DIR/te17.o"

# v1.2.1 RC2: frozen RC1 engine (tests/reference/kieekey-1.2.1-rc1) compiled
# under a renamed namespace for the lockstep A/B differential.
"$CXX" -std=c++2b -O2 -Dok=ok_rc1 -Itests/reference/kieekey-1.2.1-rc1 \
    -c tests/reference/kieekey-1.2.1-rc1/TextEngine.cpp -o "$OBJ_DIR/te_rc1.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "frozen RC1 TextEngine.cpp failed to compile"; exit 1; }
ENGINE_RC1="$OBJ_DIR/te_rc1.o"
# v1.2.1 RC2: -O1 + sanitizers regression build. RC1 shipped an ODR bug
# (FlatTables.hpp codeTableFor: external-linkage inline referencing
# internal-linkage tables) that only surfaced as a LINK failure at -O1 with
# ASan; -O2 inlined it away. Keep one such build in the gate forever.
"$CXX" -std=c++2b -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $INC \
    -c src/core/TextEngine.cpp -o "$OBJ_DIR/te_asan.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "TextEngine.cpp (-O1 sanitizers) failed to compile"; exit 1; }
ENGINE_ASAN="$OBJ_DIR/te_asan.o"

build ok_tests            -std=c++2b -O2 $INC tests/test_textengine.cpp   $ENGINE23 || rc=1
build diff_engine_ab      -std=c++2b -O2 $INC -Itests tests/diff_engine_ab.cpp \
                          $ENGINE23 $ENGINE_RC1                                     || rc=1
build test_notifications  -std=c++2b -O2 $INC tests/test_notifications.cpp $ENGINE23 || rc=1
build stress_rc2          -std=c++2b -O2 -pthread $INC tests/stress_rc2.cpp $ENGINE23  || rc=1
build test_hotfix_asan    -std=c++2b -O1 -g -fsanitize=address,undefined \
                          -fno-omit-frame-pointer $INC tests/test_hotfix.cpp $ENGINE_ASAN || rc=1
build ok_ring_tests       -std=c++2b -O2 $INC tests/test_ringbuffer.cpp             || rc=1
build ok_wrap_tests       -std=c++2b -O2 $INC -DOK_WRAP_NO_WIN32 \
                          tests/test_win32wrapper.cpp src/core/win32_wrapper.cpp    || rc=1
build test_outputitem     -std=c++2b -O2 $INC tests/test_outputitem.cpp   $ENGINE23   || rc=1
build test_hotfix         -std=c++2b -O2 $INC tests/test_hotfix.cpp       $ENGINE23   || rc=1
build test_v331_features  -std=c++2b -O2 $INC tests/test_v331_features.cpp $ENGINE23  || rc=1
build stress_engine       -std=c++2b -O2 $INC tests/stress_engine.cpp     $ENGINE23   || rc=1
build stress_queue        -std=c++2b -O2 $INC tests/stress_queue.cpp      $ENGINE23   || rc=1
build soak_engine         -std=c++2b -O2 $INC tests/soak_engine.cpp       $ENGINE23   || rc=1
build gate_correctness    -std=c++17 -O2 -DNO_205 $INC -Iimebench_kit/harness \
                          tests/gate_correctness.cpp $ENGINE17                        || rc=1
build edge_behaviors      -std=c++17 -O2 -DLINUX -w -include algorithm $INC \
                          -Itests/reference/openkey-2.0.5/engine \
                          tests/edge_behaviors.cpp $ENGINE17 $OK205_SRC $REF_SRC      || rc=1
build dirty_input         -std=c++17 -O2 -DLINUX -w -include algorithm $INC \
                          -Itests/reference/openkey-2.0.5/engine \
                          tests/dirty_input.cpp $ENGINE17 $OK205_SRC $REF_SRC         || rc=1
build real_passages       -std=c++17 -O2 -DLINUX -w -include algorithm $INC \
                          -Itests/reference/openkey-2.0.5/engine \
                          tests/real_passages.cpp $ENGINE17 $OK205_SRC $REF_SRC       || rc=1

# --- v1.2.0 Stable additions -------------------------------------------------
build test_key_correctness -std=c++2b -O2 $INC tests/test_key_correctness.cpp $ENGINE23  || rc=1
build test_lifecycle      -std=c++2b -O2 -pthread $INC tests/test_lifecycle.cpp $ENGINE23 || rc=1
build test_state_transitions -std=c++2b -O2 $INC tests/test_state_transitions.cpp $ENGINE23 || rc=1
build soak_pipeline       -std=c++2b -O2 -pthread $INC tests/soak_pipeline.cpp         || rc=1

# --- version-identifier consistency (no mixed version strings) --------------
if command -v python3 >/dev/null 2>&1; then
    printf '  [check] %-22s' "version consistency"
    if ( cd "$REPO_ROOT" && python3 scripts/check_version.py --repo=. ) \
            > "$OUT/logs/version.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/version.log"
        rc=1
    fi
fi

dispatch_builds

if [ "$rc" -ne 0 ]; then
    fail "build step failed — aborting"
    exit 1
fi

say ""
say "-- running ---------------------------------------------------------"

# run <name> <timeout-seconds> [args...]
# Recorded, then dispatched with xargs -P "$JOBS"; results are printed
# afterwards in declaration order so the report stays stable and readable.
RJOBS=()
run() {
    local name="$1"; shift
    local tmo="$1"; shift
    RJOBS+=("$name")
    {
        printf '#!/bin/bash\nset -u\nstart=$(date +%%s)\n'
        # The differential harnesses write their .md report into the CWD and
        # read tests/data relative to it — always run from the repository root.
        printf 'cd %s\n' "$(q "$REPO_ROOT")"
        printf 'timeout %s %s ' "$(q "$tmo")" "$(q "$OUT/bin/$name")"
        local a
        for a in "$@"; do printf '%s ' "$(q "$a")"; done
        printf -- '> %s 2>&1\nrc=$?\nend=$(date +%%s)\n' "$(q "$OUT/logs/$name.log")"
        printf 'printf "%%s %%s" "$rc" "$((end-start))" > %s\n' "$(q "$JOBS_DIR/run_$name.res")"
    } > "$JOBS_DIR/run_$name.sh"
}

dispatch_runs() {
    local n
    printf '%s\n' "${RJOBS[@]}" | xargs -P "$JOBS" -I{} bash "$JOBS_DIR/run_{}.sh"
    for n in "${RJOBS[@]}"; do
        local res status secs
        res=$(cat "$JOBS_DIR/run_$n.res" 2>/dev/null)
        status=${res%% *}
        secs=${res##* }
        printf '  [run]   %-22s' "$n"
        if [ "$status" = "0" ]; then
            printf ' PASS  (%ss)\n' "${secs:-0}"
        else
            printf ' FAIL  rc=%s (%ss) — %s\n' "${status:-?}" "${secs:-0}" "$OUT/logs/$n.log"
            rc=1
        fi
    done
}

# Hard per-binary caps: a future regression that turns a barrier/wait path
# into an infinite block must fail the gate in minutes, not hang CI until the
# job-level limit kills it.
run ok_tests             120
run diff_engine_ab       300
run test_notifications   120
run stress_rc2           300
run test_hotfix_asan     300
run ok_ring_tests        120
run ok_wrap_tests        120
run test_outputitem      120
run test_hotfix          120
run test_v331_features   120
if [ "$QUICK" -eq 1 ]; then
    run stress_engine    300 1 200000
    run stress_queue     300
    run soak_engine      300 20000 2000
    run gate_correctness 600 --max-words=8000
else
    run stress_engine    600 1 1000000
    run stress_queue     600
    run soak_engine      600 200000 20000
    run gate_correctness 900
fi
run edge_behaviors       600
run dirty_input          600
run real_passages        600

run test_key_correctness     300
run test_lifecycle           300
run test_state_transitions   300
if [ "$QUICK" -eq 1 ]; then
    run soak_pipeline    300 20000
else
    run soak_pipeline    900 2000000
fi

dispatch_runs

# Keep the generated reports out of the working tree (they are artifacts).
for rep in EDGE_BEHAVIORS_REPORT.md DIRTY_INPUT_REPORT.md REAL_PASSAGES_REPORT.md; do
    [ -f "$REPO_ROOT/$rep" ] && mv "$REPO_ROOT/$rep" "$OUT/$rep"
done

say ""
if [ "$rc" -eq 0 ]; then
    say "== ALL NATIVE TESTS PASSED =="
else
    say "== SUITE FAILED (see $OUT/logs) =="
fi
exit "$rc"
