#!/bin/bash
# Circuit checks (run from emulator/, in WSL / Linux):
#   1. build tools/circuit_dump with the game's circuit code
#   2. dump the three circuits and validate every layout and keep-out rule
#   3. determinism: each circuit dumps the same built alone or after the others
set -e
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/arcade3d_circuits
mkdir -p $OUT
g++ -I. -I.. -O1 -std=c++17 -Wall -Wno-unused-function -Wno-unused-variable \
  tools/circuit_dump.cpp Arduino.cpp ../track.cpp ../utils.cpp ../colors.cpp ../rendering.cpp \
  ../gfx.cpp ../render_scenery.cpp -o $OUT/circuit_dump
for c in 0 1 2; do $OUT/circuit_dump $c > $OUT/dump$c.txt; done
python3 tools/validate_circuits.py $OUT/dump0.txt $OUT/dump1.txt $OUT/dump2.txt
for c in 0 1 2; do
  a=$($OUT/circuit_dump $c | grep -E "^(S|M|H|SEC|SPRITES) " | md5sum)
  b=$($OUT/circuit_dump $c 1 2 0 | grep -E "^(S|M|H|SEC|SPRITES) " | md5sum)
  [ "$a" = "$b" ] && echo "circuit $c deterministic" || { echo "circuit $c NOT deterministic"; exit 1; }
done
