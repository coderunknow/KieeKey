#!/usr/bin/env bash
#----------------------------------------------------------------------------
# tests/run_mega_bench.sh — build & run the MASSIVE differential correctness
# benchmark (mega_correctness.cpp) against:
#   1. TextEngine (KieeKey, shipped implementation)
#   2. vi_oracle.hpp (clean-room reference oracle)
#   3. engine205 (the REAL, unmodified OpenKey 2.0.5 engine, compiled through
#      its native Linux platform path from tests/reference/openkey-2.0.5)
#
# Usage: run_mega_bench.sh [--fast] [--no-205]
#   --fast      run 1% of each budget (CI smoke; ~2-4 min)
#   --no-205    skip the OpenKey 2.0.5 differential suite
#----------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
STD="-std=c++17"
OPT="-O2"
INC="-I src/core -I tests -I tests/reference/openkey-2.0.5/engine"
WARN="-w"   # 2.0.5 upstream is warning-heavy; suppress to keep output clean

FAST=0; USE_205=1
for a in "$@"; do
  case "$a" in
    --fast)   FAST=1 ;;
    --no-205) USE_205=0 ;;
    *) echo "unknown arg: $a"; exit 2 ;;
  esac
done

BUILD=".arena/mega_build"
mkdir -p "$BUILD"

HARNESS_ARGS=""
[ "$FAST" -eq 1 ] && HARNESS_ARGS="$HARNESS_ARGS --fast"

DEFS=""
[ "$USE_205" -eq 0 ] && DEFS="-DNO_205"

echo "[bench] compiling harness..."
$CXX $STD $OPT $INC $DEFS -c tests/mega_correctness.cpp -o "$BUILD/mega_correctness.o" \
    2>&1 | grep -v "unused-parameter" | head -20 || true

LINK=("$BUILD/mega_correctness.o" src/core/TextEngine.cpp)

if [ "$USE_205" -eq 1 ]; then
  echo "[bench] compiling real OpenKey 2.0.5 engine (differential model)..."
  REF=tests/reference/openkey-2.0.5/engine
  $CXX $STD $OPT $WARN -DLINUX -I"$REF" -c "$REF/Engine.cpp"         -o "$BUILD/ref_Engine.o"
  $CXX $STD $OPT $WARN -DLINUX -I"$REF" -c "$REF/Vietnamese.cpp"     -o "$BUILD/ref_Vietnamese.o"
  $CXX $STD $OPT $WARN -DLINUX -I"$REF" -c "$REF/Macro.cpp"          -o "$BUILD/ref_Macro.o"
  $CXX $STD $OPT $WARN -DLINUX -I"$REF" -c "$REF/SmartSwitchKey.cpp" -o "$BUILD/ref_SmartSwitchKey.o"
  $CXX $STD $OPT $WARN -DLINUX -I"$REF" -include algorithm -c "$REF/ConvertTool.cpp" -o "$BUILD/ref_ConvertTool.o"
  $CXX $STD $OPT $WARN -DLINUX -I"$REF" -c tests/engine205.cpp       -o "$BUILD/engine205.o"
  LINK+=("$BUILD/engine205.o" "$BUILD/ref_Engine.o" "$BUILD/ref_Vietnamese.o" \
         "$BUILD/ref_Macro.o" "$BUILD/ref_SmartSwitchKey.o" "$BUILD/ref_ConvertTool.o")
  echo "[bench] linking with OpenKey 2.0.5 differential model..."
else
  echo "[bench] OpenKey 2.0.5 differential suite disabled by --no-205."
fi

$CXX $STD $OPT $INC "${LINK[@]}" -o "$BUILD/mega_correctness"

echo "[bench] running..."
"$BUILD/mega_correctness" $HARNESS_ARGS
rc=$?
echo "[bench] exit code $rc"
exit $rc
