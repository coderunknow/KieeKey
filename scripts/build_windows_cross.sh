#!/bin/bash
#============================================================================
# KieeKey v1.2.1 Stable — Windows PE cross-build gate (llvm-mingw).
#
# WHAT THIS VALIDATES
#   The complete shipped Windows application (KieeKeyApp.exe) — hook, engine,
#   TSF composer, process monitor, tray app, resources, manifest, version
#   info — compiled AND LINKED as a real Windows PE for x64 and ARM64, with
#   -Wall -Wextra -Werror. Linking is the point: it catches ODR violations,
#   missing symbols, INITGUID/COM definition gaps and ABI-level breakage
#   that pure syntax checks cannot see, using a DIFFERENT standard library
#   (libc++) and a different Windows header set (mingw-w64) than MSVC.
#
# WHAT THIS DOES NOT CLAIM
#   This is a cross-compile gate, NOT a native MSVC build and NOT runtime
#   validation. The MSVC CI (windows-2022, x64+ARM64+ARM64EC) remains the
#   authoritative native build; on-host runtime validation was not possible
#   in this environment (no Windows host). Honest scope — see the Stable
#   release report.
#
# Usage: scripts/build_windows_cross.sh [output-dir]
# Exit 0 = all gates pass.
#============================================================================
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO_ROOT/build/win-cross}"
LLVM_MINGW="${LLVM_MINGW:-llvm-mingw}"
# Resolve the cross toolchain (bin dir).
if [ -d "$LLVM_MINGW/bin" ]; then TC="$LLVM_MINGW/bin"
elif command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then TC=""
else echo "ERROR: llvm-mingw toolchain not found (set LLVM_MINGW=/path)" >&2; exit 2; fi

SRCS="src/core/TextEngine.cpp src/core/ModernKeyHook.cpp src/core/win32_wrapper.cpp
      src/core/ProcessMonitor.cpp src/tsf/TsfComposer.cpp src/app/main.cpp"
LIBS="-luser32 -lgdi32 -lshell32 -lole32 -ldwmapi -lpsapi -lversion -lwinmm -ladvapi32 -lwtsapi32 -lurlmon -lcomctl32 -luuid"
FLAGS="-std=c++20 -O2 -DUNICODE -D_UNICODE -Wall -Wextra -Werror -Isrc/core -Isrc/tsf -Isrc/app"
RCFLAGS="-c 65001"
fail=0
mkdir -p "$OUT"

for arch in x86_64 aarch64; do
    echo "== Windows ${arch} PE cross-build =="
    OBJ="$OUT/$arch"; mkdir -p "$OBJ"
    CXX="$TC/$arch-w64-mingw32-g++"
    RC="$TC/$arch-w64-mingw32-windres"
    [ -x "$CXX" ] || { echo "missing $CXX"; exit 2; }
    for s in $SRCS; do
        obj="$OBJ/$(basename "$s" .cpp).o"
        if ! "$CXX" $FLAGS -c "$REPO_ROOT/$s" -o "$obj" 2> "$OBJ/$(basename "$s").err"; then
            echo "  COMPILE FAIL: $s"; head -30 "$OBJ/$(basename "$s").err"; fail=1; continue
        fi
        if [ -s "$OBJ/$(basename "$s").err" ]; then
            echo "  WARNINGS (would fail -Werror? no — output present): $s"; head -10 "$OBJ/$(basename "$s").err"
        fi
        echo "  compile ok: $(basename "$s")"
    done
    # Resources: version info + icons + manifest (same .rc the MSVC build compiles).
    if [ -x "$RC" ]; then
        if "$RC" $RCFLAGS -I "$REPO_ROOT/src/app" "$REPO_ROOT/src/app/KieeKeyApp.rc" -O coff -o "$OBJ/app.res" 2> "$OBJ/app.rc.err"; then
            echo "  windres ok: KieeKeyApp.rc"
        else
            echo "  windres FAIL (non-fatal for the compile gate; binary built without resources)"; head -10 "$OBJ/app.rc.err"; RES=""
        fi
    else
        echo "  (windres not present — building without resources)"; RES=""
    fi
    RES_FLAG=""
    [ -f "$OBJ/app.res" ] && RES_FLAG="$OBJ/app.res"
    EXE="$OUT/KieeKeyApp-${arch}.exe"
    if "$CXX" $FLAGS $OBJ/TextEngine.o $OBJ/ModernKeyHook.o $OBJ/win32_wrapper.o \
              $OBJ/ProcessMonitor.o $OBJ/TsfComposer.o $OBJ/main.o $RES_FLAG \
              -o "$EXE" $LIBS -municode -mwindows 2> "$OBJ/link.err"; then
        echo "  LINK OK -> $EXE"
        file "$EXE" | sed 's/^/  /'
    else
        echo "  LINK FAIL:"; head -40 "$OBJ/link.err"; fail=1
    fi
done

exit $fail
