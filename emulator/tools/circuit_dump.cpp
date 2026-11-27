// Circuit dumper: builds a circuit with the game's own code and prints every
// section, every segment, the minimap and the sprite counts (the input of
// validate_circuits.py). circuits.cpp is compiled in so its static section
// table is visible. Build and run through tools/check_circuits.sh.
//
//   circuit_dump C [B ...]   builds circuits B ... first, then C (the dump of
//                            C must not depend on what was built before it)
#include <stdio.h>
#include <stdlib.h>
#include "../../circuits.cpp"
#include "../../render_road.h"
RoadCam roadCam = { 0, 1000, 1 };

int main(int argc, char** argv) {
  int c = argc > 1 ? atoi(argv[1]) : 0;
  if (!track_init()) return 1;
  for (int a = 2; a < argc; a++) buildCircuit(atoi(argv[a]));
  buildCircuit(c);
  printf("CIRCUIT %d %s %d\n", c, curCircuit().name, segCount);
  for (int k = 0; k < sSecN; k++)
    printf("SEC %d start %d len %d turn %d zone %d flags %d peak %.2f z %.0f\n", k, sSecStart[k], sSec[k].len,
           sSec[k].turn, sSec[k].zone, sSec[k].flags, secPeak(sSec[k]), sSecStart[k] * (float)SEG_LEN);
  for (int i = 0; i < segCount; i++) {
    const Segment& s = segments[i];
    printf("S %d %.4f %.1f %d %d %d %d %d %d %d %d %d %d\n", i, s.curve, s.y, s.flags, s.ground,
           s.build[0], s.style[0], s.build[1], s.style[1], s.spr[0], s.sprOff[0], s.spr[1], s.sprOff[1]);
  }
  for (int j = 0; j < MINIMAP_PTS; j++) printf("M %d %d\n", minimapX[j], minimapY[j]);
  for (int t = 0; t < SPR_COUNT; t++) printf("H %d %.3f %.3f\n", t, spriteHitWidth(t), spriteWidth(t));
  int counts[SPR_COUNT] = { 0 };
  for (int i = 0; i < segCount; i++)
    for (int sd = 0; sd < 2; sd++)
      if (segments[i].spr[sd] >= 0) counts[segments[i].spr[sd]]++;
  printf("SPRITES");
  for (int t = 0; t < SPR_COUNT; t++) printf(" %d:%d", t, counts[t]);
  printf("\n");
  return 0;
}
