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
# File: scripts/build_windows_exe.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#----------------------------------------------------------------------------
# scripts/build_windows_exe.sh — build the COMPLETE, RUNNABLE Windows product
# (KieeKeyApp.exe) from a non-Windows host with Zig's bundled MinGW-w64.
#
# WHY THIS EXISTS (and how it differs from tools/crossbuild_windows.sh)
#   tools/crossbuild_windows.sh compiles SIX translation units. That is enough
#   to answer "does the Windows-only code still compile?" but NOT enough to
#   produce a runnable binary: v1.3.0 added the Arcade Hub window, the Chaos
#   Lab window, the Arcade/Chaos/AI/Progression/Analytics engines and the web
#   bridge, all of which src/app/main.cpp links against. Linking the old six
#   objects fails with a wall of undefined symbols, so a maintainer on Linux
#   had no way to hand a tester an .exe.
#
#   This script builds EVERY translation unit of the `openkey` CMake target
#   (core + TSF + app), compiles the resources with `zig rc` (icon, manifest,
#   VERSIONINFO) and links a real PE32+ executable with a static runtime, so
#   the artifact runs on a clean Windows x64 machine with no MinGW DLLs.
#
#   It is still a CROSS build: the authoritative release binaries come from
#   .github/workflows/build.yml (MSVC, /W4 /WX, x64 + ARM64 + ARM64EC).
#
# USAGE
#   scripts/build_windows_exe.sh [--arch=x86_64|aarch64] [--out=DIR]
#                                [--config=Release|Debug] [--check] [--keep]
#                                [--zig=/path/to/zig]
#
#   --check   compile every TU, skip the link (fast syntax/type gate)
#   --keep    keep .o files
#
# Exit 0 = the .exe was produced (or, with --check, every TU compiled).
#----------------------------------------------------------------------------
set -u
set -o pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

ARCH="x86_64"
CONFIG="Release"
CHECK=0
KEEP=0
ZIG="${ZIG:-}"
OUT=""

for arg in "$@"; do
    case "$arg" in
        --arch=*)   ARCH="${arg#--arch=}" ;;
        --out=*)    OUT="${arg#--out=}" ;;
        --config=*) CONFIG="${arg#--config=}" ;;
        --zig=*)    ZIG="${arg#--zig=}" ;;
        --check)    CHECK=1 ;;
        --keep)     KEEP=1 ;;
        -h|--help)  sed -n '30,60p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "build_windows_exe.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done

case "$ARCH" in
    x86_64)  TARGET="x86_64-windows-gnu"  ;;
    aarch64) TARGET="aarch64-windows-gnu" ;;
    *) echo "build_windows_exe.sh: --arch must be x86_64 or aarch64" >&2; exit 2 ;;
esac
[ -n "$OUT" ] || OUT="build/windows-${ARCH}"

# --- locate zig -------------------------------------------------------------
if [ -z "$ZIG" ]; then
    if command -v zig >/dev/null 2>&1; then
        ZIG="$(command -v zig)"
    elif command -v python3 >/dev/null 2>&1 && python3 -c "import ziglang" >/dev/null 2>&1; then
        ZIG="python3 -m ziglang"
    else
        echo "build_windows_exe.sh: no zig found (pip install ziglang, or --zig=PATH)" >&2
        exit 2
    fi
fi

case "$CONFIG" in
    Release) OPT="-O2 -DNDEBUG" ;;
    Debug)   OPT="-O0 -g -DDEBUG" ;;
    *) echo "build_windows_exe.sh: unknown --config=$CONFIG" >&2; exit 2 ;;
esac

# --- the complete translation-unit list of the `openkey` CMake target -------
# ok_core (portable engine + Windows glue) …
SRCS_CORE="
src/core/TextEngine.cpp
src/core/ModernKeyHook.cpp
src/core/win32_wrapper.cpp
src/core/ProcessMonitor.cpp
src/core/Arcade.cpp
src/core/ArcadeFrame.cpp
src/core/ArcadeRender.cpp
src/core/ArcadeServer.cpp
src/core/ChaosEngine.cpp
src/core/AiRival.cpp
src/core/Progression.cpp
src/core/TypingAnalytics.cpp
src/core/OnlineGhost.cpp
src/core/Diagnostics.cpp
"
# … ok_tsf …
SRCS_TSF="
src/tsf/TsfComposer.cpp
"
# … and the application itself.
SRCS_APP="
src/app/main.cpp
src/app/ArcadeWindow.cpp
src/app/ChaosLabWindow.cpp
"
SRCS="$SRCS_CORE $SRCS_TSF $SRCS_APP"

DEFS="-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00"
INC="-Isrc/core -Isrc/tsf -Isrc/app"
LIBS="-luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -ldwmapi -lpsapi -lversion
      -lurlmon -lwinmm -lmsimg32 -ladvapi32 -lcomctl32 -lwtsapi32 -lws2_32
      -luuid -static"
CXX_ARGS="-target $TARGET -std=c++23 $OPT $DEFS $INC"

mkdir -p "$OUT"
echo "== KieeKey full Windows cross-build =="
echo "   zig    : $ZIG ($($ZIG version 2>/dev/null || echo '?'))"
echo "   target : $TARGET"
echo "   config : $CONFIG"
echo "   out    : $OUT"
echo ""

# --- compile ----------------------------------------------------------------
echo "-- compiling -----------------------------------------------------------"
OBJS=""
rc=0
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"
pids=""
for src in $SRCS; do
    _bn="${src//\//_}"; obj="$OUT/${_bn%.cpp}.o"   # pure-bash: src path -> obj name
    log="$OUT/$(basename "$obj").log"
    # shellcheck disable=SC2086
    ( $ZIG c++ $CXX_ARGS -c "$src" -o "$obj" > "$log" 2>&1 ; echo $? > "$obj.rc" ) &
    pids="$pids $!"
    # Bound the parallelism: main.cpp alone needs ~1 GB of RSS with -O2.
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; done
done
wait

for src in $SRCS; do
    _bn="${src//\//_}"; obj="$OUT/${_bn%.cpp}.o"   # pure-bash: src path -> obj name
    log="$OUT/$(basename "$obj").log"
    printf '  %-40s' "$src"
    if [ -f "$obj" ] && [ "$(cat "$obj.rc" 2>/dev/null)" = "0" ]; then
        echo "ok"
        OBJS="$OBJS $obj"
    else
        echo "FAILED"
        sed 's/^/      /' "$log" 2>/dev/null | head -30
        rc=1
    fi
done

if [ "$CHECK" -eq 1 ]; then
    echo ""
    if [ "$rc" -eq 0 ]; then
        echo "== COMPILE CHECK PASSED (every Windows TU builds) =="
    else
        echo "== COMPILE CHECK FAILED =="
    fi
    exit "$rc"
fi
[ "$rc" -ne 0 ] && exit 1

# --- resources (icon + manifest + VERSIONINFO) ------------------------------
echo "-- resources -----------------------------------------------------------"
printf '  %-40s' "src/app/KieeKeyApp.rc"
if ( cd src/app && $ZIG rc /c 65001 /fo "$REPO_ROOT/$OUT/app.res" KieeKeyApp.rc ) \
        > "$OUT/rc.log" 2>&1; then
    echo "ok"
else
    echo "FAILED"
    sed 's/^/      /' "$OUT/rc.log" | head -30
    exit 1
fi

# --- link -------------------------------------------------------------------
echo "-- linking -------------------------------------------------------------"
EXE="$OUT/KieeKeyApp.exe"
printf '  %-40s' "KieeKeyApp.exe"
# shellcheck disable=SC2086
# -mwindows alone left the PE subsystem as CONSOLE(3) under zig's windows-gnu
# driver, which pops a console window behind the tray app; force the GUI
# subsystem explicitly so wWinMain runs as a windowed (subsystem 2) process.
if $ZIG c++ -target "$TARGET" $OPT -municode -mwindows -Wl,--subsystem,windows \
        $OBJS "$OUT/app.res" -o "$EXE" $LIBS > "$OUT/link.log" 2>&1; then
    echo "ok"
else
    echo "FAILED"
    grep -iE "error|undefined" "$OUT/link.log" | head -40
    exit 1
fi

if [ "$KEEP" -eq 0 ]; then
    rm -f "$OUT"/*.o "$OUT"/*.log "$OUT"/*.rc
fi

SIZE=$(wc -c < "$EXE")
echo ""
echo "== built: $EXE ($SIZE bytes) =="
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$EXE" | sed 's/^/   sha256: /'
fi
exit 0
