/*
  ═══════════════════════════════════════════════════════════════
  RENDERIZADO DE CARRETERA, TÚNELES Y EDIFICIOS
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_ROAD_H
#define RENDER_ROAD_H

#include <Arduino.h>
#include "structs.h"

// Dibuja la carretera, túneles, edificios y objetos del escenario
void drawRoad(float position, float playerX, float playerZdist,
              float cameraDepth, int timeOfDay);

// Dibuja el cielo y el parallax background
void drawSky(float position, float playerZdist, int timeOfDay, float skyOffset);

// Dibuja un edificio 3D con ventanas
void drawBuilding(RenderPt& p0, RenderPt& p1, int heightVal, uint16_t baseCol,
                  int sIdx, bool isLeft, bool showFront);

// Dibuja sprites del escenario (árboles, arbustos, rocas, postes)
void drawSpriteShape(int type, int sx, int sy, float scale, int16_t clipY, int timeOfDay);

#endif // RENDER_ROAD_H
