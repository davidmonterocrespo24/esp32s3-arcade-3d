/*
  ═══════════════════════════════════════════════════════════════
  COLOR AND PALETTE MANAGEMENT
  RGB565 helpers + the day/sunset/night/dawn palette with smooth
  blending between phases, tinted per circuit theme (setTheme).
  ═══════════════════════════════════════════════════════════════
*/

#ifndef COLORS_H
#define COLORS_H

#include <stdint.h>
#include "structs.h"

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
//  DAY / NIGHT CYCLE
//  0 = day, 1 = sunset, 2 = night, 3 = dawn (wraps at 4). Each phase
//  holds, then blends into the next during its last CYCLE_BLEND.
//  Races drive the cycle by race progress (circuit cycleStart ->
//  cycleEnd); the demo / free modes advance it by distance.
// ═══════════════════════════════════════════════════════════════
#define CYCLE_PHASE_DIST   250000.0f  // distance per phase in free mode
#define CYCLE_BLEND        0.30f      // last 30% of each phase blends to the next

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
// Facade base colours, shaded per face from the palette light
enum FacadeColor : uint8_t {
  FAC_CONCRETE = 0, FAC_BRICK, FAC_SANDSTONE, FAC_BLUEGLASS, FAC_TEAL, FAC_SLATE,
  FAC_CREAM, FAC_RUST, FAC_STUCCO, FAC_TERRACOTTA, FAC_ROCK, FAC_WALLGREY,
  BUILDING_PALETTE };

struct ColorSet {
  uint16_t skyTop, skyMid, skyHorizon, fog;
  uint16_t grassL, grassD, roadL, roadD;
  uint16_t rumbleL, rumbleD, lane, edgeLine;
  uint16_t skylineFar, skylineNear, windowLit, windowDay;
  uint16_t sun, sunGlow, cloud;
  uint16_t tunnelWallL, tunnelWallD, tunnelCeil, tunnelLight, portal;
  uint16_t foliageL, foliageD, trunk, rock;
  uint16_t lampPole, lampLight;
  uint16_t ground[MAT_COUNT][2];          // light / dark band per material (no fog)
  uint16_t kerbL, kerbD;                  // city kerb (instead of the red/white rumble)
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
extern int   colorTheme;    // theme set by setTheme (Theme in circuits.h)

// Choose the circuit theme (tints the four time-of-day palettes and sets
// the ground materials). Call before setDayCycle; buildCircuit does it.
void setTheme(int theme);
// Set the cycle position (0..4, wraps) and rebuild `pal` now.
void setDayCycle(float cycle);
// Set the cycle position, rebuilding `pal` only when it can visibly change
// (inside a blend window, or every 1/64 of a phase). Cheap to call per frame.
void applyDayCycle(float cycle);
// Advance the cycle by distance travelled (demo / free mode).
void advanceDayCycle(float distance);

#endif // COLORS_H
