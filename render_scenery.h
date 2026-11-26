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
  /* themes add more */
  SPR_COUNT };

// Sprite anchored at its base (sx, sy) on screen. scale = road projection
// scale at that depth (ProjPt::scale); fog 0..1 towards pal.fog.
void  drawSceneSprite(int type, float sx, float sy, float scale, float fog);

// Collision width in road half-widths (full width of the solid part, e.g.
// a trunk), 0 = not solid (bushes, grass).
float spriteHitWidth(int type);

#endif // RENDER_SCENERY_H
