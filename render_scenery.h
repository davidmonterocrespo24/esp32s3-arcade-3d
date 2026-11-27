/*
  ═══════════════════════════════════════════════════════════════
  SCENERY SPRITES (every placeable prop beside the road)
  Placed per segment side in Segment::spr[] / sprOff[] by the circuit
  generator; drawn by the road pass at the near boundary of their
  segment; solid for the physics when spriteHitWidth() > 0.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_SCENERY_H
#define RENDER_SCENERY_H

#include <stdint.h>

enum SpriteType : int8_t {
  SPR_PINE = 0,     // conifer, shaded halves
  SPR_TREE,         // round deciduous tree
  SPR_BUSH,         // low bush (not solid)
  SPR_ROCK,         // boulder
  SPR_POST,         // roadside marker post with a reflector
  SPR_PALM,         // palm tree (coast)
  SPR_PINE_SNOW,    // snowy conifer (mountain, high up)
  // ---- added by the themes phase (append only: values are stored in Segment::spr) ----
  SPR_CYPRESS,      // tall slim Mediterranean cypress
  SPR_FLOWERS,      // flower patch (not solid)
  SPR_GRASS,        // grass tuft (not solid)
  SPR_BOULDER,      // big rock-fall boulder
  SPR_FENCE,        // wooden ranch fence section (not solid; placed out of reach)
  SPR_CHEVRON_L,    // chevron board, arrows to the left (left-hand corner ahead)
  SPR_CHEVRON_R,    // chevron board, arrows to the right
  SPR_BOARD_3,      // distance boards before a hard corner: three stripes ...
  SPR_BOARD_2,      // ... two stripes ...
  SPR_BOARD_1,      // ... one stripe (last one before the braking point)
  SPR_BILLBOARD_A,  // advertising hoarding on two legs, three designs,
  SPR_BILLBOARD_B,  // lit at night
  SPR_BILLBOARD_C,
  SPR_LIGHTHOUSE,   // striped lighthouse, the lamp shines at dusk / night
  SPR_UMBRELLA,     // beach umbrella (not solid)
  SPR_TRAFFIC_LIGHT,// traffic light on a pole at a crossing
  SPR_BUS_STOP,     // bus shelter
  SPR_PLANTER,      // concrete planter with a shrub
  SPR_STREET_TREE,  // small tree in a sidewalk pit
  SPR_NEON_PINK,    // vertical neon blade sign fixed to a facade (not solid)
  SPR_NEON_CYAN,
  SPR_SAILBOAT,     // sailboat out on the water (not solid)
  SPR_SNOW_POLE,    // orange / black snow marker pole
  SPR_CHALET,       // alpine wooden hut
  SPR_FLAG,         // chequered flag on a pole (start area)
  SPR_UMBRELLA_B,   // beach umbrella, blue / yellow
  SPR_COUNT };

// Length along the road of one fence piece (world units): pieces placed
// every FENCE_LEN / SEG_LEN segments join into a continuous fence.
#define FENCE_LEN 400.0f

// Sprite anchored at its base (sx, sy) on screen. scale = road projection
// scale at that depth (ProjPt::scale); fog 0..1 towards pal.fog.
void  drawSceneSprite(int type, float sx, float sy, float scale, float fog);

// Collision width in road half-widths (full width of the solid part, e.g.
// a trunk), 0 = not solid (bushes, grass).
float spriteHitWidth(int type);

// Width of the whole sprite in road half-widths (crown, canopy, board),
// used by the circuit generator to keep props clear of the road, of the
// building line and of each other.
float spriteWidth(int type);

#endif // RENDER_SCENERY_H
