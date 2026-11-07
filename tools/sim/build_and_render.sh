#!/bin/sh
# Renders every screen to tools/sim/out/*.png using the real UI code (needs the PlatformIO libdeps for Adafruit_GFX).
set -e
cd "$(dirname "$0")"
ROOT=../..
GFX="$ROOT/.pio/libdeps/tembed_xinput/Adafruit GFX Library"
mkdir -p out
clang++ -std=c++17 -O1 -DARDUINO=100 -Ishim -I"$GFX" -I"$ROOT/src" sim_main.cpp \
  "$ROOT/src/ui_gfx.cpp" "$ROOT/src/ui_screens.cpp" "$ROOT/src/ui_screens2.cpp" "$ROOT/src/ui_screens3.cpp" "$GFX/Adafruit_GFX.cpp" -o out/sim
./out/sim "${1:-0}"
python3 raw2png.py out
