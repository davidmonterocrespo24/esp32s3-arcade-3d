/*
  ═══════════════════════════════════════════════════════════════
  COLOR MANAGEMENT IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "colors.h"
#include <TFT_eSPI.h>

// ═══════════════════════════════════════════════════════════════
//  COLOR PALETTE (Variable Definitions)
// ═══════════════════════════════════════════════════════════════
uint16_t colSky1, colSky2, colSky3;
uint16_t colGrassL, colGrassD;
uint16_t colRoadL, colRoadD;
uint16_t colRumbleL, colRumbleD;
uint16_t colLane, colFog;

// ═══════════════════════════════════════════════════════════════
//  FUNCTION IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t darkenCol(uint16_t c, float f) {
  int r = (int)(((c >> 11) & 0x1F) * f);
  int g = (int)(((c >> 5)  & 0x3F) * f);
  int b = (int)((c & 0x1F) * f);
  return ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
}

uint16_t lerpCol(uint16_t c1, uint16_t c2, float t) {
  if (t <= 0) return c1;
  if (t >= 1) return c2;
  int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  return (((int)(r1 + (r2 - r1) * t) & 0x1F) << 11) |
         (((int)(g1 + (g2 - g1) * t) & 0x3F) << 5)  |
          ((int)(b1 + (b2 - b1) * t) & 0x1F);
}

void initColors(int timeOfDay) {
  switch (timeOfDay) {
    case 0: // Day
      colSky1    = rgb(115, 185, 255);
      colSky2    = rgb(80, 140, 230);
      colSky3    = rgb(50, 100, 200);
      colGrassL  = rgb(16, 200, 16);
      colGrassD  = rgb(0, 154, 0);
      colRoadL   = rgb(107, 107, 107);
      colRoadD   = rgb(105, 105, 105);
      colRumbleL = TFT_WHITE;
      colRumbleD = TFT_RED;
      colLane    = rgb(204, 204, 204);
      colFog     = rgb(180, 215, 255);
      break;
    case 1: // Sunset
      colSky1    = rgb(255, 140, 60);
      colSky2    = rgb(255, 90, 30);
      colSky3    = rgb(140, 30, 10);
      colGrassL  = rgb(80, 120, 20);
      colGrassD  = rgb(50, 80, 10);
      colRoadL   = rgb(95, 85, 80);
      colRoadD   = rgb(85, 75, 70);
      colRumbleL = rgb(240, 200, 150);
      colRumbleD = rgb(200, 50, 30);
      colLane    = rgb(200, 180, 140);
      colFog     = rgb(200, 120, 60);
      break;
    default: // Night
      colSky1    = rgb(10, 15, 50);
      colSky2    = rgb(5, 8, 30);
      colSky3    = rgb(0, 2, 15);
      colGrassL  = rgb(0, 60, 15);
      colGrassD  = rgb(0, 40, 8);
      colRoadL   = rgb(55, 55, 65);
      colRoadD   = rgb(45, 45, 55);
      colRumbleL = rgb(100, 100, 120);
      colRumbleD = rgb(80, 10, 10);
      colLane    = rgb(110, 110, 130);
      colFog     = rgb(15, 15, 40);
      break;
  }
}
