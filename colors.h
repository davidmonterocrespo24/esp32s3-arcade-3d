/*
  ═══════════════════════════════════════════════════════════════
  COLOR AND PALETTE MANAGEMENT
  RGB565 helpers + the day/sunset/night/dawn palette with smooth
  blending between phases (restores the day/night cycle that was
  lost when the sky became a fixed sunset bitmap in commit 684eed5).
  ═══════════════════════════════════════════════════════════════
*/

#ifndef COLORS_H
#define COLORS_H

#include <stdint.h>

// Named colours (RGB565) used across the game
#define COL_BLACK   0x0000
#define COL_WHITE   0xFFFF
#define COL_RED     0xF800
#define COL_YELLOW  0xFFE0
#define COL_ORANGE  0xFD20
#define COL_GREEN   0x07E0
#define COL_GREY    0x7BEF
#define COL_CYAN    0x07FF

// ═══════════════════════════════════════════════════════════════
//  RGB565 HELPERS
// ═══════════════════════════════════════════════════════════════
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// alpha in 0..32 (32 = fg only). Classic packed-channel blend.
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint32_t a32) {
  uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
  uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
  uint32_t r = ((((f - b) * a32) >> 5) + b) & 0x07E0F81Fu;
  return (uint16_t)(r | (r >> 16));
}

// Saturating add (for glows / lights)
static inline uint16_t add565(uint16_t a, uint16_t b) {
  uint32_t r = (a & 0xF800u) + (b & 0xF800u); if (r > 0xF800u) r = 0xF800u;
  uint32_t g = (a & 0x07E0u) + (b & 0x07E0u); if (g > 0x07E0u) g = 0x07E0u;
  uint32_t l = (a & 0x001Fu) + (b & 0x001Fu); if (l > 0x001Fu) l = 0x001Fu;
  return (uint16_t)(r | g | l);
}

// Scale a colour by s/32 (s in 0..32)
static inline uint16_t scale565(uint16_t c, uint32_t s32) {
  return blend565(c, 0, s32);
}

uint16_t darkenCol(uint16_t c, float f);
uint16_t lerpCol(uint16_t c1, uint16_t c2, float t);

// ═══════════════════════════════════════════════════════════════
//  ACTIVE PALETTE (RGB565, recomputed when the time of day moves)
// ═══════════════════════════════════════════════════════════════
#define BUILDING_PALETTE 8

struct ColorSet {
  uint16_t skyTop, skyMid, skyHorizon, fog;
  uint16_t grassL, grassD, roadL, roadD;
  uint16_t rumbleL, rumbleD, lane, edgeLine;
  uint16_t skylineFar, skylineNear, windowLit, windowDay;
  uint16_t sun, sunGlow, cloud;
  uint16_t tunnelWallL, tunnelWallD, tunnelCeil, tunnelLight, portal;
  uint16_t foliageL, foliageD, trunk, rock;
  uint16_t lampPole, lampLight;
  uint16_t facadeLit[BUILDING_PALETTE];   // face turned to the sun
  uint16_t facadeShade[BUILDING_PALETTE]; // face away from the sun
  uint16_t facadeFront[BUILDING_PALETTE]; // face towards the camera
  uint16_t roof[BUILDING_PALETTE];
  // Lighting for the 3D cars, linear 0..1 per channel
  float lightR, lightG, lightB;     // directional (sun/moon) colour
  float ambR, ambG, ambB;           // ambient colour
  float sunDirX, sunDirY, sunDirZ;  // direction TO the light, view space
  float night;                      // 0 = full day .. 1 = full night
  float sunY;                       // sun/moon height on screen (pixels)
  float stars;                      // star visibility 0..1
};

extern ColorSet pal;
extern int   timeOfDay;     // 0=day 1=sunset 2=night 3=dawn (dominant phase)
extern float dayCycle;      // continuous cycle position in [0, 4)

// Set the cycle position (0..4) and rebuild `pal`.
void setDayCycle(float cycle);
// Advance the cycle by distance travelled (call once per frame).
void advanceDayCycle(float distance);

#endif // COLORS_H
