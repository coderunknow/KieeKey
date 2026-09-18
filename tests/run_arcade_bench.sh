#!/usr/bin/env bash
#============================================================================
# KieeKey - A modified version based on OpenKey
#
# Modified work:
#   KieeKey - refactored and completed logic
#   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
#   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
#
# File: tests/run_arcade_bench.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#----------------------------------------------------------------------------
# tests/run_arcade_bench.sh — the standardized v1.3.0 arcade benchmark.
#
# Builds tools/arcade_bench.cpp and writes the raw evidence into
# docs/bench/arcade-130/ (environment + measurements, machine readable), the
# same layout the engine campaigns use.
#
# Usage: tests/run_arcade_bench.sh [--iters=N] [--server-iters=N] [--out=DIR]
#----------------------------------------------------------------------------
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO_ROOT/docs/bench/arcade-130"
CXX="${CXX:-g++}"
ITERS=4000
SERVER_ITERS=2000

for arg in "$@"; do
    case "$arg" in
        --iters=*)        ITERS="${arg#*=}" ;;
        --server-iters=*) SERVER_ITERS="${arg#*=}" ;;
        --out=*)          OUT="${arg#*=}" ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

mkdir -p "$OUT"
BIN="$(mktemp -d)/arcade_bench"

echo "== KieeKey arcade benchmark =="
echo "   compiler : $($CXX --version | head -1)"
echo "   output   : $OUT"

"$CXX" -std=c++23 -O2 -Wall -Wextra -Wpedantic -Isrc/core \
    "$REPO_ROOT/tools/arcade_bench.cpp" \
    "$REPO_ROOT/src/core/Arcade.cpp" \
    "$REPO_ROOT/src/core/ArcadeFrame.cpp" \
    "$REPO_ROOT/src/core/ArcadeRender.cpp" \
    "$REPO_ROOT/src/core/ArcadeServer.cpp" \
    "$REPO_ROOT/src/core/Progression.cpp" \
    "$REPO_ROOT/src/core/ChaosEngine.cpp" \
    "$REPO_ROOT/src/core/AiRival.cpp" \
    "$REPO_ROOT/src/core/TypingAnalytics.cpp" \
    -o "$BIN" -pthread || { echo "build FAILED" >&2; exit 1; }

"$BIN" --iters="$ITERS" --server-iters="$SERVER_ITERS" | tee "$OUT/arcade_bench.txt"

# Freeze the environment next to the numbers (the reports quote both).
{
    echo "{"
    echo "  \"campaign\": \"v1.3.0-arcade-130\","
    echo "  \"date_utc\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "  \"commit\": \"$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)\","
    echo "  \"compiler\": \"$($CXX --version | head -1)\","
    echo "  \"kernel\": \"$(uname -srm)\","
    echo "  \"cpu\": \"$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')\","
    echo "  \"iterations\": $ITERS,"
    echo "  \"server_iterations\": $SERVER_ITERS"
    echo "}"
} > "$OUT/environment.json"

echo
echo "== done — evidence in $OUT =="
