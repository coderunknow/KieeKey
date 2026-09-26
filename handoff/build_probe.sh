#!/usr/bin/env bash
#============================================================================
# Rebuild kieekey_ui_probe.exe (x64, CONSOLE) from the current tree with zig
# (see scripts/build_windows_exe.sh for the full-app build — same toolchain
# path, same flags; here we build the kieekey_ui_probe CMake target's TU set,
# CMakeLists.txt L298-313, with KIEEKEY_UI_PROBE defined, and link as a
# console app because the probe has its own main()).
#
# Why this exists: the Phase-0 field handoff needs the probe exe built from
# the EXACT tree the user is running (beta8fix2 / PE 1.3.0.11). The CI-built
# probe cannot always be fetched from the sandbox (Azure artifact storage
# unreachable), so the repo's documented local cross-build path is used.
#
# USAGE: bash handoff/build_probe.sh
#        (zig is required: `python3 -m pip install --user --break-system-packages ziglang`)
#============================================================================
set -u
set -o pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"
ZIG="python3 -m ziglang"
TARGET=x86_64-windows-gnu
OUT="$REPO/handoff"
OBJDIR="${OBJDIR:-/tmp/kk-probe-build}"
mkdir -p "$OBJDIR"
DEFS="-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DKIEEKEY_UI_PROBE"
INC="-Isrc/core -Isrc/tsf -Isrc/app -Itools/ui_probe"
LIBS="-luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -ldwmapi -lpsapi -lversion -lurlmon -lwinmm -lmsimg32 -ladvapi32 -lcomctl32 -lwtsapi32 -lws2_32 -luuid -static"
SRCS="
src/core/TextEngine.cpp src/core/ModernKeyHook.cpp src/core/win32_wrapper.cpp
src/core/ProcessMonitor.cpp src/core/Arcade.cpp src/core/ArcadeFrame.cpp
src/core/ArcadeRender.cpp src/core/ArcadeServer.cpp src/core/ChaosEngine.cpp
src/core/AiRival.cpp src/core/Progression.cpp src/core/TypingAnalytics.cpp
src/core/OnlineGhost.cpp src/core/Diagnostics.cpp src/tsf/TsfComposer.cpp
src/app/main.cpp src/app/ArcadeWindow.cpp src/app/ChaosLabWindow.cpp
tools/ui_probe/ui_probe.cpp"
OPT="${OPT:--O2 -DNDEBUG}"
JOBS="${JOBS:-2}"
OBJS=""; rc=0
for src in $SRCS; do
  _bn="${src//\//_}"; obj="$OBJDIR/${_bn%.cpp}.o"; log="$OBJDIR/$(basename "$obj").log"
  # shellcheck disable=SC2086
  ( $ZIG c++ -target $TARGET -std=c++23 $OPT $DEFS $INC -c "$src" -o "$obj" >"$log" 2>&1; echo $? >"$obj.rc" ) &
  while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.3; done
done
wait
for src in $SRCS; do
  _bn="${src//\//_}"; obj="$OBJDIR/${_bn%.cpp}.o"
  if [ -f "$obj" ] && [ "$(cat "$obj.rc" 2>/dev/null)" = "0" ]; then
    OBJS="$OBJS $obj"
  else
    echo "COMPILE FAILED: $src"
    sed 's/^/  /' "$OBJDIR/$(basename "$obj").log" 2>/dev/null | head -25
    rc=1
  fi
done
[ "$rc" -ne 0 ] && { echo "PROBE BUILD FAILED (compile)"; exit 1; }
EXE="$OUT/kieekey_ui_probe.exe"
# shellcheck disable=SC2086
$ZIG c++ -target $TARGET $OPT $OBJS -o "$EXE" $LIBS > "$OBJDIR/link.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "LINK FAILED:"; grep -iE 'error|undefined' "$OBJDIR/link.log" | head -40; exit 1
fi
file "$EXE" 2>/dev/null || true
sha256sum "$EXE"
echo "PROBE BUILD OK -> $EXE"
