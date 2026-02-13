/*
  ═══════════════════════════════════════════════════════════════
  TRAFFIC CAR RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_TRAFFIC_H
#define RENDER_TRAFFIC_H

#include <Arduino.h>

// Draws a traffic car in 3D
void drawTrafficCar(int cx, int cy, float scale, uint16_t col, int16_t clipY);

#endif // RENDER_TRAFFIC_H
