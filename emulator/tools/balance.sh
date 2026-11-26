#!/bin/bash
# Balance matrix (run from emulator/ after `make headless`): the autopilot
# races every circuit at every difficulty; prints the player's place and time
# and the winner. Targets: EASY P1-P2, NORMAL P2-P4, HARD P4-P6.
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/arcade3d_balance
mkdir -p $OUT
for c in 0 1 2; do
  for d in 0 1 2; do
    mkdir -p $OUT/o_${c}_${d}
    ./car_game_headless $OUT/o_${c}_${d} 5200 --race $c --auto --diff $d > $OUT/log_${c}_${d}.txt 2>&1 &
  done
done
wait
for c in 0 1 2; do
  for d in 0 1 2; do
    f=$OUT/log_${c}_${d}.txt
    you=$(grep -E "^ +[0-9]\. YOU" $f | sed 's/  */ /g')
    win=$(grep -E "^ +1\. " $f | awk '{print $2, $4}')
    echo "circuit $c diff $d |$you | winner $win s"
  done
done
