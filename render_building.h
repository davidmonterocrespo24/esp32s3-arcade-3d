/*
  ═══════════════════════════════════════════════════════════════
  RENDERIZADO DE EDIFICIOS
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_BUILDING_H
#define RENDER_BUILDING_H

#include <Arduino.h>
#include "structs.h"

// Dibuja un edificio en 3D
void drawBuilding(RenderPt& p0, RenderPt& p1, int heightVal, uint16_t baseCol, int sIdx, bool isLeft, bool showFront);

#endif // RENDER_BUILDING_H
