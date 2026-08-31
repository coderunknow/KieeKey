#!/usr/bin/env bash
#----------------------------------------------------------------------------
# tests/run_three_bench.sh — build & run the three-engine benchmark:
#   KieeKey (TextEngine) vs OpenKey 2.0.5 (vendored) vs UniKey (UKEngine)
# Emits THREE_ENGINE_BENCH_REPORT.md in the repo root.
#
# Usage: tests/run_three_bench.sh
#----------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
STD="-std=c++17"
OPT="-O2"
INC="-I src/core -I tests -I tests/reference/openkey-2.0.5/engine -I tests/reference/unikey"
WARN="-w"
UK_FIX="-include tests/reference/unikey/uk_fix.h"

BUILD=".arena/three_bench"
mkdir -p "$BUILD"

REF205=tests/reference/openkey-2.0.5/engine
REFUK=tests/reference/unikey

echo "[bench] compiling OpenKey 2.0.5 engine (differential model)..."
$CXX $STD $OPT $WARN -DLINUX -I"$REF205" -c "$REF205/Engine.cpp"         -o "$BUILD/ref_Engine.o"
$CXX $STD $OPT $WARN -DLINUX -I"$REF205" -c "$REF205/Vietnamese.cpp"     -o "$BUILD/ref_Vietnamese.o"
$CXX $STD $OPT $WARN -DLINUX -I"$REF205" -c "$REF205/Macro.cpp"          -o "$BUILD/ref_Macro.o"
$CXX $STD $OPT $WARN -DLINUX -I"$REF205" -c "$REF205/SmartSwitchKey.cpp" -o "$BUILD/ref_SmartSwitchKey.o"
$CXX $STD $OPT $WARN -DLINUX -I"$REF205" -include algorithm -c "$REF205/ConvertTool.cpp" -o "$BUILD/ref_ConvertTool.o"
$CXX $STD $OPT $WARN -DLINUX -I"$REF205" -c tests/engine205.cpp          -o "$BUILD/engine205.o"

echo "[bench] compiling UniKey UKEngine (reference, unmodified)..."
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/ukengine.cpp"   -o "$BUILD/uk_engine.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/inputproc.cpp"  -o "$BUILD/uk_inputproc.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/macro.cpp"      -o "$BUILD/uk_macro.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/mactab.cpp"     -o "$BUILD/uk_mactab.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/usrkeymap.cpp"  -o "$BUILD/uk_usrkeymap.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/charset.cpp"    -o "$BUILD/uk_charset.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/convert.cpp"    -o "$BUILD/uk_convert.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/data.cpp"       -o "$BUILD/uk_data.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/error.cpp"      -o "$BUILD/uk_error.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/pattern.cpp"    -o "$BUILD/uk_pattern.o"
$CXX $STD $OPT $WARN -DLINUX -fpermissive $UK_FIX -I"$REFUK" -c "$REFUK/byteio.cpp"     -o "$BUILD/uk_byteio.o"

echo "[bench] compiling harness + KieeKey engine..."
$CXX $STD $OPT $INC tests/bench_three_engines.cpp src/core/TextEngine.cpp \
    "$BUILD/engine205.o" "$BUILD/ref_Engine.o" "$BUILD/ref_Vietnamese.o" \
    "$BUILD/ref_Macro.o" "$BUILD/ref_SmartSwitchKey.o" "$BUILD/ref_ConvertTool.o" \
    "$BUILD/uk_engine.o" "$BUILD/uk_inputproc.o" "$BUILD/uk_macro.o" "$BUILD/uk_mactab.o" \
    "$BUILD/uk_usrkeymap.o" "$BUILD/uk_charset.o" "$BUILD/uk_convert.o" "$BUILD/uk_data.o" \
    "$BUILD/uk_error.o" "$BUILD/uk_pattern.o" "$BUILD/uk_byteio.o" \
    -o "$BUILD/bench_three_engines"

echo "[bench] running (2M-key latency pass per engine — ~1–2 min)..."
# The harness writes THREE_ENGINE_BENCH_REPORT.md itself (CWD = repo root).
"$BUILD/bench_three_engines"
echo "[bench] report written to THREE_ENGINE_BENCH_REPORT.md"
