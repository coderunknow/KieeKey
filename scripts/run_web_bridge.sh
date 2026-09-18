#!/usr/bin/env bash
#============================================================================
# KieeKey - scripts/run_web_bridge.sh
#
# Run the Arcade Hub web player (tools/arcade_serve) as a long-lived service
# on this machine: it keeps the C++ engine in memory, serves web/ and speaks
# the same /api/* the sandbox preview does.
#
#   bash scripts/run_web_bridge.sh                 # default 0.0.0.0:8765
#   PORT=9000 WEB=/path/to/web bash scripts/...
#
# Why this exists: the hosted sandbox preview is convenient but ephemeral (it
# dies with the sandbox). Run this on a machine you control — a PC, a small
# VPS, a NAS — to get an always-on player at http://<that-machine>:<port>/.
#
# Env:
#   PORT       listen port                     (default 8765)
#   HOST       bind address                    (default 0.0.0.0 — LAN visible)
#   FPS        engine tick rate                (default 60)
#   WEB        web assets directory            (default <repo>/web)
#   BUILD_DIR  cmake/out build directory       (default <repo>/build/web-bridge)
#   MAX_RUNS   restart on crash at most N times (default 0 = forever)
#============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-8765}"
HOST="${HOST:-0.0.0.0}"
FPS="${FPS:-60}"
WEB="${WEB:-$REPO/web}"
BUILD_DIR="${BUILD_DIR:-$REPO/build/web-bridge}"
MAX_RUNS="${MAX_RUNS:-0}"
BIN="$BUILD_DIR/arcade_serve"

CORE_SRC=(
    src/core/Arcade.cpp
    src/core/ArcadeFrame.cpp
    src/core/ArcadeRender.cpp
    src/core/ArcadeServer.cpp
    src/core/Progression.cpp
    src/core/ChaosEngine.cpp
    src/core/AiRival.cpp
    src/core/TypingAnalytics.cpp
)

build_with_cmake() {
    command -v cmake >/dev/null 2>&1 || return 1
    echo "[bridge] configuring with cmake in $BUILD_DIR"
    cmake -S "$REPO" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
          -DKIEEKEY_BUILD_TESTS=OFF -DKIEEKEY_BUILD_UI=none >/dev/null || return 1
    cmake --build "$BUILD_DIR" --target arcade_serve --config Release -j 2 || return 1
    # MSVC/multi-config generators put the binary in a config subdirectory.
    for candidate in "$BUILD_DIR/arcade_serve" \
                     "$BUILD_DIR/arcade_serve.exe" \
                     "$BUILD_DIR/Release/arcade_serve.exe" \
                     "$BUILD_DIR/Release/arcade_serve"; do
        if [ -x "$candidate" ] || [ -f "$candidate" ]; then BIN="$candidate"; return 0; fi
    done
    return 1
}

build_with_gxx() {
    command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || return 1
    local cxx; cxx="$(command -v g++ || command -v clang++)"
    echo "[bridge] cmake not usable — building with $cxx"
    mkdir -p "$BUILD_DIR"
    ( cd "$REPO" && "$cxx" -std=c++23 -O2 -Isrc/core tools/arcade_serve.cpp "${CORE_SRC[@]}" \
        -o "$BUILD_DIR/arcade_serve" -pthread ) || return 1
    BIN="$BUILD_DIR/arcade_serve"
}

if [ ! -x "$BIN" ]; then
    build_with_cmake || build_with_gxx || {
        echo "[bridge] could not build arcade_serve (need cmake, or g++/clang++)" >&2
        exit 1
    }
fi

lan_ip() {
    if command -v hostname >/dev/null 2>&1; then
        hostname -I 2>/dev/null | awk '{print $1; exit}'
    fi
}

echo "[bridge] binary : $BIN"
echo "[bridge] serving: http://localhost:$PORT/  (LAN: http://$(lan_ip || echo '<this-host>'):$PORT/)"
echo "[bridge] web    : $WEB"
echo "[bridge] Ctrl+C to stop. Restart-on-crash: MAX_RUNS=$MAX_RUNS (0 = forever)"

runs=0
while :; do
    runs=$((runs + 1))
    "$BIN" --port "$PORT" --host "$HOST" --web "$WEB" --fps "$FPS" || \
        echo "[bridge] exited with code $? (run $runs)"
    if [ "$MAX_RUNS" -gt 0 ] && [ "$runs" -ge "$MAX_RUNS" ]; then
        echo "[bridge] MAX_RUNS=$MAX_RUNS reached — stopping"
        break
    fi
    echo "[bridge] restarting in 2 s"
    sleep 2
done
