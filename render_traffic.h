/*
  ═══════════════════════════════════════════════════════════════
  RENDERIZADO DE AUTOS DE TRÁFICO
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_TRAFFIC_H
#define RENDER_TRAFFIC_H

#include <Arduino.h>

// Dibuja un auto de tráfico en 3D
void drawTrafficCar(int cx, int cy, float scale, uint16_t col, int16_t clipY);

#endif // RENDER_TRAFFIC_H
