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
#   CXX_LAUNCHER=sccache optionally caches individual compilation commands.
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

# A launcher is one executable, not an eval'd shell command.
COMPILER=("$CXX")
if [ -n "${CXX_LAUNCHER:-}" ]; then
    COMPILER=("$CXX_LAUNCHER" "$CXX")
fi

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
        # Bash builtin quoting avoids a sed process/subshell chain per argument.
        printf '#!/bin/bash\nset -u\ncd %q\n' "$REPO_ROOT"
        if [ -n "${CXX_LAUNCHER:-}" ]; then
            # Caches cannot reuse a multi-source compile+link command. Preserve
            # all flags and input ordering, but compile each TU independently.
            # Per-target object paths avoid writes racing between build jobs.
            local -a flags=() sources=() objects=() inputs=()
            local arg obj i value=0
            for arg in "$@"; do
                if [ "$value" -eq 1 ]; then
                    flags+=("$arg"); value=0; continue
                fi
                case "$arg" in
                    -include|-imacros|-I|-isystem|-iquote|-idirafter|-x)
                        flags+=("$arg"); value=1 ;;
                    *.cpp)
                        obj="$OBJ_DIR/$name-${#sources[@]}.o"
                        sources+=("$arg"); objects+=("$obj"); inputs+=("$obj") ;;
                    *.o) inputs+=("$arg") ;;
                    *) flags+=("$arg") ;;
                esac
            done
            printf 'if {\n'
            for i in "${!sources[@]}"; do
                printf '%q ' "${COMPILER[@]}" "${flags[@]}" -c "${sources[$i]}" -o "${objects[$i]}"
                printf '&&\n'
            done
            # Link normally: linking is not cacheable and must always run.
            printf '%q ' "$CXX" "${flags[@]}" "${inputs[@]}" -o "$OUT/bin/$name"
            printf '\n} >> %q 2>&1\n' "$BUILD_LOG"
        else
            printf 'if %q ' "$CXX"
            printf '%q ' "$@"
            printf -- '-o %q >> %q 2>&1\n' "$OUT/bin/$name" "$BUILD_LOG"
        fi
        printf 'then printf 0 > %q\nelse printf 1 > %q\nfi\n' \
            "$JOBS_DIR/$name.rc" "$JOBS_DIR/$name.rc"
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
"${COMPILER[@]}" -std=c++2b -O2 $INC -c src/core/TextEngine.cpp -o "$OBJ_DIR/te23.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "TextEngine.cpp (C++23) failed to compile"; exit 1; }
"${COMPILER[@]}" -std=c++17 -O2 $INC -c src/core/TextEngine.cpp -o "$OBJ_DIR/te17.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "TextEngine.cpp (C++17) failed to compile"; exit 1; }
ENGINE23="$OBJ_DIR/te23.o"
ENGINE17="$OBJ_DIR/te17.o"

# v1.2.1 RC2: frozen RC1 engine (tests/reference/kieekey-1.2.1-rc1) compiled
# under a renamed namespace for the lockstep A/B differential.
"${COMPILER[@]}" -std=c++2b -O2 -Dok=ok_rc1 -Itests/reference/kieekey-1.2.1-rc1 \
    -c tests/reference/kieekey-1.2.1-rc1/TextEngine.cpp -o "$OBJ_DIR/te_rc1.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "frozen RC1 TextEngine.cpp failed to compile"; exit 1; }
ENGINE_RC1="$OBJ_DIR/te_rc1.o"
# v1.2.1 RC2: -O1 + sanitizers regression build. RC1 shipped an ODR bug
# (FlatTables.hpp codeTableFor: external-linkage inline referencing
# internal-linkage tables) that only surfaced as a LINK failure at -O1 with
# ASan; -O2 inlined it away. Keep one such build in the gate forever.
"${COMPILER[@]}" -std=c++2b -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $INC \
    -c src/core/TextEngine.cpp -o "$OBJ_DIR/te_asan.o" \
    >> "$BUILD_LOG" 2>&1 || { fail "TextEngine.cpp (-O1 sanitizers) failed to compile"; exit 1; }
ENGINE_ASAN="$OBJ_DIR/te_asan.o"

build ok_tests            -std=c++2b -O2 $INC tests/test_textengine.cpp   $ENGINE23 || rc=1
build diff_engine_ab      -std=c++2b -O2 $INC -Itests tests/diff_engine_ab.cpp \
                          $ENGINE23 $ENGINE_RC1                                     || rc=1
build test_notifications  -std=c++2b -O2 $INC tests/test_notifications.cpp $ENGINE23 || rc=1
# v1.2.2 RC2: pure-header perf-profile resolver test + full option-matrix harness.
build test_perf_profiles    -std=c++2b -O2 $INC tests/test_perf_profiles.cpp              || rc=1
build test_option_matrix    -std=c++2b -O2 -pthread $INC -Itests \
                            tests/test_option_matrix.cpp $ENGINE23                        || rc=1
build stress_rc2          -std=c++2b -O2 -pthread $INC tests/stress_rc2.cpp $ENGINE23  || rc=1
build test_hotfix_asan    -std=c++2b -O1 -g -fsanitize=address,undefined \
                          -fno-omit-frame-pointer $INC tests/test_hotfix.cpp $ENGINE_ASAN || rc=1
build ok_ring_tests       -std=c++2b -O2 $INC tests/test_ringbuffer.cpp             || rc=1
build ok_wrap_tests       -std=c++2b -O2 $INC -DOK_WRAP_NO_WIN32 \
                          tests/test_win32wrapper.cpp src/core/win32_wrapper.cpp    || rc=1
# v1.3.0-beta5 (bug B2): FULL-CHAIN live-effects transport — real TextEngine
# → real planOutput → real OutputRing (SPSC) → consumer thread → real
# InlineEmitter (shim-recorded SendInput) → decoded screen, compared against
# a direct-apply reference + the pure gate model (liveGateBlocker).
build test_live_effects_chain -std=c++2b -O2 -pthread $INC -DOK_WRAP_NO_WIN32 \
                          tests/test_live_effects_chain.cpp src/core/win32_wrapper.cpp \
                          src/core/ChaosEngine.cpp $ENGINE23                      || rc=1
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

# --- v1.3.0 Arcade, Chaos, AI & Progression additions -----------------------
build test_arcade         -std=c++2b -O2 $INC tests/test_arcade.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/Progression.cpp $ENGINE23 || rc=1
build test_arcade_render  -std=c++2b -O2 $INC tests/test_arcade_render.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/Progression.cpp $ENGINE23 || rc=1
build test_arcade_server  -std=c++2b -O2 $INC tests/test_arcade_server.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/ArcadeServer.cpp src/core/Progression.cpp src/core/ChaosEngine.cpp src/core/AiRival.cpp src/core/TypingAnalytics.cpp $ENGINE23 || rc=1
# v1.3.0-beta8 (bug UX-09): tests/test_arcade_beta7.cpp SHIPPED UNWIRED — it
# was in no runner and no CMake target, so every "regression coverage" claim
# made for the beta7 fixes was unverified. It did not even compile-and-pass
# when finally run (it asserted restartRequired=true with no game running).
build test_arcade_beta7   -std=c++2b -O2 $INC tests/test_arcade_beta7.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/ArcadeServer.cpp src/core/Progression.cpp src/core/ChaosEngine.cpp src/core/AiRival.cpp src/core/TypingAnalytics.cpp $ENGINE23 || rc=1
# v1.3.0-beta8: the UI/UX audit regression suite (UX-01..UX-08).
build test_arcade_beta8_ux -std=c++2b -O2 $INC tests/test_arcade_beta8_ux.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/ArcadeServer.cpp src/core/Progression.cpp src/core/ChaosEngine.cpp src/core/AiRival.cpp src/core/TypingAnalytics.cpp $ENGINE23 || rc=1
# The Win32 hub/lab windows are executed here through tests/win32_gdi_stub.cpp
# (a recording USER32/GDI32 layer), so the graphical front-end is covered on a
# host without the Windows SDK. -D_WIN32 selects the real window implementation.
build test_arcade_window  -std=c++2b -O2 $INC -Isrc/app -D_WIN32 -Itests/win32_headers -include tests/win32_gdi_shim.hpp \
                          tests/test_arcade_window.cpp tests/win32_gdi_stub.cpp \
                          src/app/ArcadeWindow.cpp src/app/ChaosLabWindow.cpp \
                          src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp \
                          src/core/ChaosEngine.cpp src/core/Progression.cpp $ENGINE23 -pthread || rc=1
build test_live_effects   -std=c++2b -O2 -pthread $INC tests/test_live_effects.cpp $ENGINE23 src/core/ChaosEngine.cpp || rc=1
# v1.3.0-beta3: shipped-path live-effects suite (drives the REAL planOutput()
# decision extracted from main.cpp) + the foreground over-backspace regression.
build test_live_output_plan -std=c++2b -O2 -pthread $INC tests/test_live_output_plan.cpp $ENGINE23 src/core/ChaosEngine.cpp || rc=1
# v1.3.0-beta3: portable proof for the settings-dialog/Chaos-Lab layout solver
# (bug #1) over the REAL authored rectangles copied from main.cpp.
build test_dialog_layout  -std=c++2b -O2 -Isrc/app $INC tests/test_dialog_layout.cpp || rc=1
# v1.3.0-beta5: three suites that SHIPPED UNWIRED (found by the beta5 deep
# sweep — a test nobody runs is a test that cannot fail): HookCounters
# semantics (bug B3), the ok::diag module, and the ProcessMonitor pure models
# (bug B4 — name fallbacks + elevation-probe decision table).
build test_hook_counters  -std=c++2b -O2 -pthread $INC tests/test_hook_counters.cpp || rc=1
build test_diagnostics    -std=c++2b -O2 -pthread $INC tests/test_diagnostics.cpp src/core/Diagnostics.cpp || rc=1
build test_diagnostics_beta7_repro -std=c++2b -O2 -pthread $INC tests/test_diagnostics_beta7_repro.cpp src/core/Diagnostics.cpp || rc=1
build test_process_monitor -std=c++2b -O2 $INC tests/test_process_monitor.cpp || rc=1
# v1.3.0-beta3 (bug #2): in-window Vietnamese composition for the typing games —
# VnComposer (Telex/VNI -> diacritics) and the real game objects driven with the
# real keystrokes (VN default, EN fallback, arrow-steering in WasdRace).
build test_vn_composer    -std=c++2b -O2 -pthread $INC tests/test_vn_composer.cpp src/core/TextEngine.cpp || rc=1
# v1.3.0-beta5 (bug B9): effect layer — every tab-0 option flip must change
# the engine output (probe-verified A/B pairs).
build test_settings_wiring -std=c++2b -O2 -pthread $INC tests/test_settings_wiring.cpp src/core/TextEngine.cpp || rc=1
build test_arcade_vn      -std=c++2b -O2 -pthread $INC tests/test_arcade_vn.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/Progression.cpp $ENGINE23 || rc=1
# v1.3.0-beta4: backspace-recovery / arcade-VN / manager-race regression suite.
build test_arcade_recovery -std=c++2b -O2 -pthread $INC tests/test_arcade_recovery.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/Progression.cpp $ENGINE23 || rc=1
# v1.3.0-beta5: pins for the tester's arcade clusters — B5 red divergent-tail
# rendering + Backspace recovery fuzz, B6 NoMistake end-of-run verdict / live
# stats / manager live-config, B7 selectable WASD steering (VN) fuzz.
build test_arcade_beta5   -std=c++2b -O2 -pthread $INC tests/test_arcade_beta5.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/Progression.cpp $ENGINE23 || rc=1
build test_chaos          -std=c++2b -O2 $INC tests/test_chaos.cpp src/core/ChaosEngine.cpp || rc=1
build test_ai_rival       -std=c++2b -O2 $INC tests/test_ai_rival.cpp src/core/AiRival.cpp || rc=1
build test_progression    -std=c++2b -O2 $INC tests/test_progression.cpp src/core/Progression.cpp || rc=1
# v1.3.0-beta8 (FT-01): the persistence RULES (file names, opt-in gate, 30 s
# crash-safe sweep) plus a save -> restart -> load round trip through the
# engines the app actually calls.
build test_progression_persist -std=c++2b -O2 -Isrc/app -Isrc/core -pthread $INC tests/test_progression_persist.cpp src/core/Progression.cpp || rc=1
# v1.3.0-beta8 (DS-01/02/05): the diagnostics TEXT layer — quick-check token
# mapping, the one builder shared by the pane and the export, truth markers.
build test_diag_report_text -std=c++2b -O2 -Isrc/app $INC tests/test_diag_report_text.cpp || rc=1
# v1.3.0-beta8 (CA-06): the app measures its own layout into the report the
# user sends — overlap / clipped / cut / unreachable, pure rectangle math.
build test_diag_self_check -std=c++2b -O2 -Isrc/app $INC tests/test_diag_self_check.cpp || rc=1
# v1.3.0-beta8 (BS-07): Arcade Hub footer/counter geometry — the hint band and
# the FPS band come from one function, disjoint at 100-200 % for the longest
# hints the games can produce.
build test_arcade_chrome_layout -std=c++2b -O2 -Isrc/app $INC tests/test_arcade_chrome_layout.cpp || rc=1
# v1.3.0-beta8 (FT-02): "Gõ chữ Flexing ra app" must never fail silently — every
# outcome carries a Vietnamese reason, five are failures, only a refused
# activation interrupts.
build test_flex_send_outcome -std=c++2b -O2 -Isrc/app $INC tests/test_flex_send_outcome.cpp || rc=1
build test_analytics      -std=c++2b -O2 $INC tests/test_analytics.cpp src/core/TypingAnalytics.cpp || rc=1
build test_online_ghost   -std=c++2b -O2 $INC tests/test_online_ghost.cpp src/core/OnlineGhost.cpp || rc=1
build arcade_cli          -std=c++2b -O2 $INC demo/arcade_cli.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp src/core/Progression.cpp src/core/ChaosEngine.cpp src/core/AiRival.cpp src/core/TypingAnalytics.cpp src/core/OnlineGhost.cpp $ENGINE23 -pthread || rc=1
build test_soak_arcade    -std=c++2b -O2 $INC tests/test_soak_arcade.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ChaosEngine.cpp src/core/AiRival.cpp src/core/Progression.cpp src/core/TypingAnalytics.cpp $ENGINE23 || rc=1

# --- version-identifier consistency (no mixed version strings) --------------
if command -v python3 >/dev/null 2>&1; then
    printf '  [check] %-22s' "input isolation"
    if ( cd "$REPO_ROOT" && python3 scripts/check_input_isolation.py --repo=. ) \
            > "$OUT/logs/input-isolation.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/input-isolation.log"
        rc=1
    fi
    printf '  [check] %-22s' "version consistency"
    if ( cd "$REPO_ROOT" && python3 scripts/check_version.py --repo=. ) \
            > "$OUT/logs/version.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/version.log"
        rc=1
    fi
    # v1.3.0-beta5 (bug B3): source contract for the tab-3 telemetry rows —
    # each row must display ITS OWN source counter (keyboard row never the
    # ring counter again). main.cpp is Windows-only; this pins the binding.
    printf '  [check] %-22s' "telemetry rows"
    if ( cd "$REPO_ROOT" && python3 scripts/audit_telemetry_rows.py ) \
            > "$OUT/logs/telemetry-rows.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/telemetry-rows.log"
        cat "$OUT/logs/telemetry-rows.log"
        rc=1
    fi
    # v1.3.0-beta6 (V1): the two dialog audits used to pass only because a
    # human remembered to run them by hand — a gate that is not in the suite
    # protects nothing. audit_layout --strict = authored rectangles cannot
    # clip/overlap/escape their page; audit_controls = every settings control
    # id that is READ is also CREATED in WM_CREATE.
    printf '  [check] %-22s' "layout (strict)"
    if ( cd "$REPO_ROOT" && python3 scripts/audit_layout.py --strict ) \
            > "$OUT/logs/layout-strict.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/layout-strict.log"
        cat "$OUT/logs/layout-strict.log"
        rc=1
    fi
    # v1.3.0-beta8 (CA-01): the four new layout checks (multi-scale text fit,
    # combo drop-down windows over lower-z-order siblings, group containment,
    # one-line row fit) are only evidence if they can FAIL. This harness seeds
    # one violation of each class into a throwaway copy of the tree and
    # asserts the audit goes red — the audit cannot be vacuously green.
    printf '  [check] %-22s' "layout audit seeds"
    if ( cd "$REPO_ROOT" && python3 tests/verify_audit_seeds.py ) \
            > "$OUT/logs/layout-seeds.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/layout-seeds.log"
        cat "$OUT/logs/layout-seeds.log"
        rc=1
    fi
    # v1.3.0-beta8 (FT-01): the engines could always save — beta7 simply never
    # called them from src/app/. main.cpp needs windows.h, so this grep-gate is
    # what keeps the boot/exit/tick call sites alive.
    printf '  [check] %-22s' "feature persistence"
    if ( cd "$REPO_ROOT" && python3 scripts/audit_feature_persistence.py ) \
            > "$OUT/logs/feature-persistence.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/feature-persistence.log"
        cat "$OUT/logs/feature-persistence.log"
        rc=1
    fi
    # v1.3.0-beta8 (WS-D FT-03/FT-05): the Live-Effects tab must tell the truth
    # — one gate model shared with the diagnostics report, the gate row on the
    # BS-02 fit path, F9 gated on the arcade keyboard mirror (never on a call
    # into the game singletons from the hook), and the cost note equal to the
    # figure docs/PERFORMANCE.md measured.
    printf '  [check] %-22s' "live effects truth"
    if ( cd "$REPO_ROOT" && python3 scripts/audit_live_effects_truth.py ) \
            > "$OUT/logs/live-effects-truth.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/live-effects-truth.log"
        cat "$OUT/logs/live-effects-truth.log"
        rc=1
    fi
    printf '  [check] %-22s' "dialog controls"
    if ( cd "$REPO_ROOT" && python3 scripts/audit_controls.py ) \
            > "$OUT/logs/dialog-controls.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/dialog-controls.log"
        cat "$OUT/logs/dialog-controls.log"
        rc=1
    fi
    # v1.3.0-beta6 (V3): the Chaos Lab window is a SECOND interactive surface
    # driving the engine singletons — same 3-layer contract as the settings
    # dialog: created + consumed + engine-connected + persisted.
    printf '  [check] %-22s' "chaos lab wiring"
    if ( cd "$REPO_ROOT" && python3 scripts/audit_chaos_lab.py ) \
            > "$OUT/logs/chaos-lab.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/chaos-lab.log"
        cat "$OUT/logs/chaos-lab.log"
        rc=1
    fi
    # v1.3.0-beta5 (bug B9): source contract for EVERY interactive settings
    # control — created + read + reflected + live-applied + persisted +
    # consumed by engine code. main.cpp is Windows-only; this pins the wiring
    # layer (tests/test_settings_wiring.cpp pins the effect layer).
    printf '  [check] %-22s' "settings wiring"
    if ( cd "$REPO_ROOT" && python3 scripts/audit_settings_wiring.py ) \
            > "$OUT/logs/settings-wiring.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/settings-wiring.log"
        cat "$OUT/logs/settings-wiring.log"
        rc=1
    fi
fi

# --- web player (headless): labs.js behaviour without a browser --------------
# The sandbox has no browser, so tests/web_labs_test.js runs the real client
# file against a small DOM/fetch double. Skipped (not failed) without Node.
if command -v node >/dev/null 2>&1; then
    printf '  [check] %-22s' "web labs (node)"
    if ( cd "$REPO_ROOT" && node tests/web_labs_test.js ) > "$OUT/logs/web_labs.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/web_labs.log"
        rc=1
    fi
    printf '  [check] %-22s' "web progress (node)"
    if ( cd "$REPO_ROOT" && node tests/web_progress_test.js ) > "$OUT/logs/web_progress.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/web_progress.log"
        rc=1
    fi
    # The renderer runs against frames captured from the C++ engine
    # (tests/data/web_frames.json, refreshed by tests/capture_web_frames.py).
    printf '  [check] %-22s' "web renderer (node)"
    if ( cd "$REPO_ROOT" && node tests/web_render_test.js ) > "$OUT/logs/web_render.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/web_render.log"
        rc=1
    fi
    # Pixel evidence for the HTML5 player: the frames above are rasterized
    # through the real web/arcade.js with a real Canvas2D implementation
    # @napi-rs/canvas remains optional locally. CI sets REQUIRE_WEB_FRAMES=1:
    # a zero exit with SKIPPED (or no render-completion evidence) must fail.
    printf '  [check] %-22s' "web frames (node)"
    if ( cd "$REPO_ROOT" && node tests/render_web_frames.js "$OUT/web-frames" ) > "$OUT/logs/web_frames.log" 2>&1; then
        if grep -q 'SKIPPED' "$OUT/logs/web_frames.log"; then
            if [ "${REQUIRE_WEB_FRAMES:-0}" = "1" ]; then
                printf ' FAILED — web frames are required; see %s\n' "$OUT/logs/web_frames.log"
                rc=1
            else
                printf ' skipped (no canvas module)\n'
            fi
        elif ! grep -Eq '^=== rendered [1-9][0-9]* game frames \+ a contact sheet ===$' "$OUT/logs/web_frames.log"; then
            printf ' FAILED — missing render-completion evidence; see %s\n' "$OUT/logs/web_frames.log"
            rc=1
        else
            printf ' ok\n'
            if [ "${REQUIRE_WEB_FRAMES:-0}" = "1" ]; then
                cat "$OUT/logs/web_frames.log"
            fi
        fi
    else
        printf ' FAILED — %s\n' "$OUT/logs/web_frames.log"
        rc=1
    fi
elif [ "${REQUIRE_WEB_FRAMES:-0}" = "1" ]; then
    printf '  [check] %-22s FAILED — Node.js is required\n' "web frames (node)"
    rc=1
fi

# --- release manifest matches the tracked tree ------------------------------
# SHA256SUMS.txt used to be hand-maintained and silently rotted (stale hashes
# for 16 files, ~62 tracked files missing). It is generated now, so the gate
# can simply assert it is in sync.
if git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf '  [check] %-22s' "SHA256SUMS manifest"
    if "$REPO_ROOT/scripts/gen_sha256sums.sh" --check \
            > "$OUT/logs/sha256sums.log" 2>&1; then
        printf ' ok\n'
    else
        printf ' FAILED — %s\n' "$OUT/logs/sha256sums.log"
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
        printf 'cd %q\n' "$REPO_ROOT"
        printf 'timeout %q %q ' "$tmo" "$OUT/bin/$name"
        if [ "$#" -gt 0 ]; then printf '%q ' "$@"; fi
        printf -- '> %q 2>&1\nrc=$?\nend=$(date +%%s)\n' "$OUT/logs/$name.log"
        printf 'printf "%%s %%s" "$rc" "$((end-start))" > %q\n' "$JOBS_DIR/run_$name.res"
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
run test_perf_profiles    60
run test_option_matrix    1200
run test_notifications   120
run stress_rc2           300
run test_hotfix_asan     300
run ok_ring_tests        120
run ok_wrap_tests        120
run test_live_effects_chain 300
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
    run test_soak_arcade 120 50
else
    run soak_pipeline    900 2000000
    run test_soak_arcade 300 500
fi

# --- v1.3.0 new layer runners ----------------------------------------------
run test_arcade              120
run test_arcade_render        60
run test_arcade_server        60
run test_arcade_beta7         60
run test_arcade_beta8_ux      60
run test_arcade_window        60
run test_live_effects        120
run test_live_output_plan    120
run test_dialog_layout       60
run test_hook_counters       60
run test_diagnostics         120
run test_diagnostics_beta7_repro  60
run test_process_monitor     60
run test_vn_composer         60
run test_settings_wiring     60
run test_arcade_vn           60
run test_arcade_recovery     90
run test_arcade_beta5        90
run test_chaos               60
run test_ai_rival            60
run test_progression         60
run test_progression_persist 60
run test_diag_report_text    30
run test_diag_self_check     30
run test_arcade_chrome_layout 30
run test_flex_send_outcome   30
run test_analytics           60
run test_online_ghost        60
run arcade_cli               60 --test

dispatch_runs

# Keep the generated reports out of the working tree (they are artifacts).
for rep in EDGE_BEHAVIORS_REPORT.md DIRTY_INPUT_REPORT.md REAL_PASSAGES_REPORT.md \
           MEGA_BENCH_REPORT.md; do
    [ -f "$REPO_ROOT/$rep" ] && mv "$REPO_ROOT/$rep" "$OUT/$rep"
done

say ""
if [ "$rc" -eq 0 ]; then
    say "== ALL NATIVE TESTS PASSED =="
else
    say "== SUITE FAILED (see $OUT/logs) =="
fi
exit "$rc"
