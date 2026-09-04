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
LIBS="-luser32 -lgdi32 -lshell32 -lole32 -ldwmapi -lpsapi -lversion -lurlmon
      -lwinmm -ladvapi32 -lcomctl32 -loleaut32 -luuid -lwtsapi32 -static"

SRCS="
src/core/ModernKeyHook.cpp
src/core/ProcessMonitor.cpp
src/core/TextEngine.cpp
src/core/win32_wrapper.cpp
src/tsf/TsfComposer.cpp
src/app/main.cpp
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
if ( cd src/app && $ZIG rc /c 65001 /fo "$REPO_ROOT/$OUT/app.res" KieeKeyApp.rc ) \
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
if $ZIG c++ -target "$TARGET" $OPT -w -municode -mwindows \
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
echo "== built: $OUT/KieeKeyApp.exe ($(wc -c < "$OUT/KieeKeyApp.exe") bytes) =="
exit 0
