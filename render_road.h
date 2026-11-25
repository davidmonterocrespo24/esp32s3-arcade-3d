/*
  ═══════════════════════════════════════════════════════════════
  ROAD, TUNNEL, BUILDINGS, SCENERY AND TRAFFIC (one back-to-front pass)
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_ROAD_H
#define RENDER_ROAD_H

#include <stdint.h>
#include "structs.h"

// A projected road point: centre x, y, half-width (px), scale, camera z
struct ProjPt { float x, y, w, scale, z; };

// Draws road, tunnels, buildings, sprites and traffic for the frame.
void drawRoad(float position, float playerX, float playerZdist, float cameraDepth);

// Scenery sprite anchored at its base (sx, sy), fog 0..1
void drawSceneSprite(int type, float sx, float sy, float scale, float fog);

#endif // RENDER_ROAD_H
