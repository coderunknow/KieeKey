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
# File: tools/crossbuild_windows.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#============================================================================
#----------------------------------------------------------------------------
# tools/crossbuild_windows.sh — build the REAL Windows product (KieeKeyApp.exe)
# from a non-Windows host, using Zig's bundled MinGW-w64 as the cross toolchain.
#
# WHY THIS EXISTS
#   Every user-visible component of KieeKey (the low-level hook, the TSF
#   composer, the tray app and its resource script) is Windows-only, so a
#   Linux/macOS developer — and this project's own release verification — had
#   no way to answer "does it even compile?" without a Windows box. That is a
#   bad place to be for a release whose headline promise is stability: a
#   one-character typo in src/app/main.cpp ships silently until CI's
#   Windows-2022 job notices, or (for an untagged branch) never.
#
#   Zig ships a complete, self-contained windows-gnu toolchain (MinGW-w64
#   headers + import libraries + `zig rc`, a drop-in replacement for rc.exe),
#   so `zig c++ -target x86_64-windows-gnu` produces a real PE32+ executable
#   with the embedded icon, manifest and VERSIONINFO — no MSVC, no SDK, no
#   wine.
#
#   This is a BUILD-VERIFICATION and artifact-convenience path. The shipped
#   release binaries are still produced by .github/workflows/build.yml (MSVC,
#   /W4 /WX, x64 + ARM64 + ARM64EC, static runtime), because that is the
#   configuration the product is actually validated on.
#
# USAGE
#   tools/crossbuild_windows.sh [--target=x86_64-windows-gnu] [--out=DIR]
#                               [--config=Release] [--check] [--keep]
#
#   --check   compile-only (syntax/type check every Windows TU; fastest
#             feedback loop while editing)
#   --keep    keep the object files in the build directory
#
# Requires: zig (pip install ziglang, or a system zig on PATH).
#----------------------------------------------------------------------------
set -u
set -o pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

TARGET="x86_64-windows-gnu"
OUT="build/windows-${TARGET%%-*}"
CONFIG="Release"
CHECK=0
KEEP=0

for arg in "$@"; do
    case "$arg" in
        --target=*) TARGET="${arg#--target=*}" ;;
        --out=*)    OUT="${arg#--out=*}" ;;
        --config=*) CONFIG="${arg#--config=*}" ;;
        --check)    CHECK=1 ;;
        --keep)     KEEP=1 ;;
        *) echo "crossbuild_windows.sh: unknown arg $arg" >&2; exit 2 ;;
    esac
done

# --- locate zig -------------------------------------------------------------
ZIG="${ZIG:-}"
if [ -z "$ZIG" ]; then
    if command -v zig >/dev/null 2>&1; then
        ZIG="$(command -v zig)"
    elif command -v python3 >/dev/null 2>&1 && python3 -c "import ziglang" >/dev/null 2>&1; then
        ZIG="python3 -m ziglang"
    elif command -v python >/dev/null 2>&1 && python -c "import ziglang" >/dev/null 2>&1; then
        ZIG="python -m ziglang"
    else
        echo "crossbuild_windows.sh: no zig found." >&2
        echo "  install with:  pip install ziglang   (or put a system zig on PATH)" >&2
        exit 2
    fi
fi

echo "== KieeKey cross-build =="
echo "   zig    : $ZIG ($($ZIG version 2>/dev/null || echo '?'))"
echo "   target : $TARGET"
echo "   config : $CONFIG"
echo "   out    : $OUT"
echo ""

case "$CONFIG" in
    Release) OPT="-O2 -DNDEBUG" ;;
    Debug)   OPT="-O0 -g" ;;
    *) echo "unknown --config=$CONFIG" >&2; exit 2 ;;
esac

mkdir -p "$OUT"
CXX_ARGS="-target $TARGET -std=c++23 $OPT -w"
DEFS="-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS"
INC="-Isrc/core -Isrc/tsf -Isrc/app"
# Same set CMakeLists.txt gives the app (ok_core PUBLIC + ok_tsf + the app):
# msimg32 (GradientFill, Arcade background) and ws2_32 (Arcade web bridge) were
# missing here, so this script could not link since v1.3.0 added them.
LIBS="-luser32 -lgdi32 -lshell32 -lole32 -ldwmapi -lpsapi -lversion -lurlmon
      -lwinmm -lmsimg32 -ladvapi32 -lcomctl32 -loleaut32 -luuid -lwtsapi32
      -lws2_32 -static"

# v1.3.0-beta8: the list below is the app's REAL source set (CMake targets
# ok_core + ok_tsf + openkey). It had gone stale — every v1.3.0 file (Arcade,
# ChaosEngine, AiRival, Progression, TypingAnalytics, OnlineGhost, Diagnostics,
# ArcadeWindow, ChaosLabWindow) was missing, so the link failed with undefined
# symbols for anyone who tried to use this path.
SRCS="
src/core/ModernKeyHook.cpp
src/core/win32_wrapper.cpp
src/core/ProcessMonitor.cpp
src/core/TextEngine.cpp
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
src/tsf/TsfComposer.cpp
src/app/main.cpp
src/app/ArcadeWindow.cpp
src/app/ChaosLabWindow.cpp
"

# --- compile ----------------------------------------------------------------
echo "-- compiling -----------------------------------------------------------"
OBJS=""
rc=0
for src in $SRCS; do
    obj="$OUT/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    printf '  %-34s' "$src"
    if $ZIG c++ $CXX_ARGS $DEFS $INC -c "$src" -o "$obj" 2> "$OUT/$(basename "$obj").log"; then
        echo "ok"
        OBJS="$OBJS $obj"
    else
        echo "FAILED"
        sed 's/^/      /' "$OUT/$(basename "$obj").log" | head -40
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

# --- resources --------------------------------------------------------------
# zig rc is a drop-in rc.exe: /c = code page (UTF-8) so the Vietnamese strings
# in KieeKeyApp.rc survive, /fo = output. The .rc #includes the manifest and
# both .ico files, all resolved relative to src/app/.
echo "-- resources -----------------------------------------------------------"
printf '  %-34s' "src/app/KieeKeyApp.rc"
if ( cd src/app && $ZIG rc /c 65001 /fo "$OUT/app.res" KieeKeyApp.rc ) \
        > "$OUT/rc.log" 2>&1; then
    echo "ok"
else
    echo "FAILED"
    sed 's/^/      /' "$OUT/rc.log" | head -40
    exit 1
fi

# --- link -------------------------------------------------------------------
echo "-- linking -------------------------------------------------------------"
printf '  %-34s' "KieeKeyApp.exe"
# v1.3.0-beta8: `-mwindows` alone made zig's driver produce a CONSOLE-subsystem
# image (0x3) — the tray app would have opened a black console window next to
# it. --subsystem,windows is explicit; the GUI entry point is picked by the
# subsystem plus -municode (and the PE header is verified below).
if $ZIG c++ -target "$TARGET" $OPT -w -municode -Wl,--subsystem,windows \
        $OBJS "$OUT/app.res" -o "$OUT/KieeKeyApp.exe" $LIBS > "$OUT/link.log" 2>&1; then
    echo "ok"
else
    echo "FAILED"
    grep -iE "error|undefined" "$OUT/link.log" | head -40
    exit 1
fi

if [ "$KEEP" -eq 0 ]; then
    rm -f $OUT/*.o "$OUT"/*.log
fi

echo ""
# The subsystem is part of the contract (a GUI image must not open a console):
# print it, and fail loudly if the linker ever produces 0x3 again.
SUBSYS=$(python3 - "$OUT/KieeKeyApp.exe" <<'PYEOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
print(struct.unpack_from('<H', data, pe + 24 + 68)[0])
PYEOF
)
echo "== built: $OUT/KieeKeyApp.exe ($(wc -c < "$OUT/KieeKeyApp.exe") bytes, subsystem $SUBSYS) =="
if [ "$SUBSYS" != "2" ]; then
    echo "crossbuild_windows.sh: FAILED — subsystem $SUBSYS is not GUI (2)" >&2
    exit 1
fi
exit 0
