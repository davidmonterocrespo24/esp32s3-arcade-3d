/*
  ═══════════════════════════════════════════════════════════════
  ROAD, TUNNEL, AND BUILDING RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_ROAD_H
#define RENDER_ROAD_H

#include <Arduino.h>
#include "structs.h"

// Draws the road, tunnels, buildings, and scenery objects
void drawRoad(float position, float playerX, float playerZdist,
              float cameraDepth, int timeOfDay);

// Draws the sky and parallax background
void drawSky(float position, float playerZdist, int timeOfDay, float skyOffset);

// Draws a 3D building with windows
void drawBuilding(RenderPt& p0, RenderPt& p1, int heightVal, uint16_t baseCol,
                  int sIdx, bool isLeft, bool showFront);

// Draws scenery sprites (trees, bushes, rocks, posts)
void drawSpriteShape(int type, int sx, int sy, float scale, int16_t clipY, int timeOfDay);

#endif // RENDER_ROAD_H
