/*
  ═══════════════════════════════════════════════════════════════
  BUILDING RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_BUILDING_H
#define RENDER_BUILDING_H

#include <Arduino.h>
#include "structs.h"

// Draws a building in 3D
void drawBuilding(RenderPt& p0, RenderPt& p1, int heightVal, uint16_t baseCol, int sIdx, bool isLeft, bool showFront);

#endif // RENDER_BUILDING_H
