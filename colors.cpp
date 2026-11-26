/*
  ═══════════════════════════════════════════════════════════════
  COLOR MANAGEMENT IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "colors.h"
#include "config.h"
#include <math.h>

ColorSet pal;
int   timeOfDay  = 0;
float dayCycle   = 0.0f;
int   colorTheme = 0;

// What the palette was last built for (applyDayCycle skips rebuilds)
static float sAppliedQ = -1.0f;
static int   sAppliedTheme = -1;

// ═══════════════════════════════════════════════════════════════
//  BASIC HELPERS
// ═══════════════════════════════════════════════════════════════

uint16_t darkenCol(uint16_t c, float f) {
  if (f <= 0) return 0;
  if (f >= 1) return c;
  return blend565(c, 0, (uint32_t)(f * 32.0f + 0.5f));
}

uint16_t lerpCol(uint16_t c1, uint16_t c2, float t) {
  if (t <= 0) return c1;
  if (t >= 1) return c2;
  return blend565(c2, c1, (uint32_t)(t * 32.0f + 0.5f));
}

// ═══════════════════════════════════════════════════════════════
//  PALETTE DEFINITIONS (RGB888, blended in 8-bit before packing)
// ═══════════════════════════════════════════════════════════════
struct RGB8 { uint8_t r, g, b; };

struct Palette {
  RGB8 skyTop, skyMid, skyHorizon, fog;
  RGB8 grassL, grassD, roadL, roadD;
  RGB8 rumbleL, rumbleD, lane, edgeLine;
  RGB8 skylineFar, skylineNear, windowLit, windowDay;
  RGB8 sun, sunGlow, cloud;
  RGB8 tunnelWallL, tunnelWallD, tunnelCeil, tunnelLight, portal;
  RGB8 foliageL, foliageD, trunk, rock;
  RGB8 lampPole, lampLight;
  float light[3], amb[3], sunDir[3];
  float night, sunY, stars;
};

static const Palette PALETTES[4] = {
  { // 0: DAY
    {58, 124, 226}, {122, 178, 246}, {205, 226, 250}, {192, 214, 238},
    {54, 166, 50}, {38, 140, 38}, {112, 112, 116}, {103, 103, 108},
    {242, 242, 242}, {212, 34, 34}, {236, 236, 236}, {226, 226, 226},
    {150, 172, 204}, {96, 112, 142}, {255, 222, 140}, {150, 188, 226},
    {255, 252, 225}, {255, 236, 170}, {255, 255, 255},
    {152, 150, 156}, {124, 122, 130}, {72, 72, 78}, {255, 232, 150}, {134, 128, 120},
    {44, 140, 44}, {26, 102, 32}, {104, 72, 42}, {124, 118, 110},
    {74, 78, 84}, {255, 214, 120},
    {1.00f, 0.97f, 0.90f}, {0.46f, 0.49f, 0.56f}, {0.45f, 0.80f, -0.40f},
    0.0f, 26.0f, 0.0f
  },
  { // 1: SUNSET
    {38, 36, 104}, {196, 86, 112}, {255, 168, 78}, {238, 150, 96},
    {84, 118, 34}, {62, 94, 26}, {102, 90, 92}, {93, 82, 86},
    {242, 212, 172}, {202, 42, 32}, {242, 212, 172}, {236, 206, 168},
    {156, 84, 116}, {62, 34, 72}, {255, 204, 112}, {255, 158, 104},
    {255, 196, 86}, {255, 112, 56}, {255, 150, 120},
    {132, 112, 104}, {108, 92, 88}, {60, 50, 52}, {255, 212, 120}, {112, 92, 86},
    {62, 100, 32}, {40, 74, 24}, {82, 52, 32}, {112, 92, 86},
    {62, 52, 56}, {255, 202, 110},
    {1.00f, 0.66f, 0.42f}, {0.44f, 0.34f, 0.44f}, {0.80f, 0.35f, 0.05f},
    0.35f, 96.0f, 0.12f
  },
  { // 2: NIGHT
    {4, 7, 24}, {14, 20, 56}, {42, 46, 92}, {26, 30, 62},
    {10, 44, 20}, {6, 34, 14}, {50, 50, 60}, {44, 44, 54},
    {124, 124, 140}, {112, 20, 26}, {152, 152, 168}, {142, 142, 158},
    {32, 34, 64}, {12, 12, 30}, {255, 212, 122}, {30, 30, 52},
    {232, 236, 255}, {110, 122, 200}, {40, 46, 82},
    {92, 92, 112}, {72, 72, 92}, {34, 34, 44}, {255, 222, 132}, {56, 56, 66},
    {12, 50, 22}, {8, 34, 16}, {40, 30, 22}, {56, 56, 62},
    {42, 42, 50}, {255, 204, 112},
    {0.42f, 0.48f, 0.74f}, {0.17f, 0.19f, 0.31f}, {-0.50f, 0.70f, 0.30f},
    1.0f, 34.0f, 1.0f
  },
  { // 3: DAWN
    {62, 82, 152}, {202, 150, 180}, {255, 202, 162}, {232, 192, 182},
    {52, 140, 62}, {40, 114, 50}, {102, 100, 108}, {95, 94, 102},
    {236, 226, 226}, {200, 42, 52}, {232, 222, 222}, {226, 216, 216},
    {172, 152, 182}, {92, 82, 112}, {255, 216, 142}, {226, 190, 204},
    {255, 232, 192}, {255, 180, 150}, {255, 212, 212},
    {142, 132, 138}, {116, 106, 114}, {66, 60, 66}, {255, 226, 150}, {122, 112, 116},
    {42, 120, 52}, {28, 94, 38}, {92, 62, 40}, {116, 106, 106},
    {66, 66, 74}, {255, 206, 120},
    {1.00f, 0.82f, 0.74f}, {0.40f, 0.38f, 0.48f}, {-0.70f, 0.35f, 0.60f},
    0.15f, 88.0f, 0.05f
  }
};

// Facade base colours (RGB888), indexed by FacadeColor. Shaded per face.
static const RGB8 FACADES[BUILDING_PALETTE] = {
  {150, 150, 156}, // concrete
  {164, 92, 72},   // brick
  {192, 172, 132}, // sandstone
  {84, 124, 176},  // blue glass
  {72, 142, 142},  // teal
  {78, 78, 94},    // dark slate
  {214, 204, 184}, // cream
  {154, 112, 82},  // rust
  {238, 234, 224}, // white stucco (villas)
  {196, 100, 62},  // terracotta (villa roofs)
  {132, 118, 104}, // rock (cliffs)
  {146, 144, 138}  // retaining wall grey
};

// Ground materials in daylight (light band, dark band). Other times of day
// are derived with the road's day -> phase ratio, so they darken and tint
// exactly like the tarmac next to them. MAT_GRASS uses the hand-tuned grass.
static const RGB8 GROUND_DAY[MAT_COUNT][2] = {
  { {54, 166, 50},   {38, 140, 38} },    // grass (replaced by the palette grass)
  { {178, 174, 166}, {164, 160, 152} },  // sidewalk slabs
  { {228, 208, 158}, {216, 196, 146} },  // sand
  { {44, 118, 182},  {38, 106, 168} },   // water
  { {128, 120, 110}, {116, 108, 100} },  // rock
  { {240, 244, 252}, {224, 230, 242} },  // snow
  { {152, 120, 82},  {138, 108, 74} }    // dirt
};
static const RGB8 KERB_DAY[2] = { {214, 212, 204}, {132, 130, 126} };

// Per-theme tints (multipliers) for grass, fog and the two skyline layers
struct ThemeTint { float grass[3], fog[3], far[3], nearL[3]; };
static const ThemeTint TINTS[3] = {
  // city: park grass, slightly bluer haze at night
  { {0.92f, 0.96f, 0.92f}, {0.97f, 0.98f, 1.04f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f} },
  // coast: dry yellow-green grass, warm haze, bluish islands
  { {1.12f, 1.00f, 0.72f}, {1.04f, 1.00f, 0.95f}, {0.82f, 0.94f, 1.10f}, {0.80f, 0.92f, 1.06f} },
  // mountain: deep alpine green, cold haze, blue ranges
  { {0.78f, 0.94f, 0.86f}, {0.94f, 0.98f, 1.07f}, {0.86f, 0.94f, 1.10f}, {0.80f, 0.90f, 1.00f} }
};

// ═══════════════════════════════════════════════════════════════
//  BLENDING
// ═══════════════════════════════════════════════════════════════
static inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  return (uint8_t)(a + (b - a) * t + 0.5f);
}
static inline RGB8 lerpRGB(RGB8 a, RGB8 b, float t) {
  RGB8 r = { lerp8(a.r, b.r, t), lerp8(a.g, b.g, t), lerp8(a.b, b.b, t) };
  return r;
}
static inline uint8_t sat8(float v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f)); }
static inline RGB8 mulRGB(RGB8 c, const float* k) {
  RGB8 r = { sat8(c.r * k[0]), sat8(c.g * k[1]), sat8(c.b * k[2]) };
  return r;
}
static inline uint16_t pack(RGB8 c) { return rgb(c.r, c.g, c.b); }

static inline uint16_t shadeRGB(RGB8 c, const float* light, const float* amb, float k) {
  float r = c.r * (amb[0] + light[0] * k);
  float g = c.g * (amb[1] + light[1] * k);
  float b = c.b * (amb[2] + light[2] * k);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

void setTheme(int theme) {
  if (theme < 0 || theme > 2) theme = 0;
  colorTheme = theme;
  setDayCycle(dayCycle);
}

void setDayCycle(float cycle) {
  if (!(cycle == cycle)) cycle = 0;            // NaN guard
  cycle = fmodf(cycle, 4.0f);
  if (cycle < 0.0f) cycle += 4.0f;
  if (cycle >= 4.0f) cycle = 0.0f;
  dayCycle = cycle;

  int   phase = (int)cycle;
  float frac  = cycle - phase;
  float t     = (frac - (1.0f - CYCLE_BLEND)) / CYCLE_BLEND;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  t = t * t * (3.0f - 2.0f * t);   // smoothstep

  const Palette& A = PALETTES[phase & 3];
  const Palette& B = PALETTES[(phase + 1) & 3];
  const ThemeTint& T = TINTS[colorTheme];
  timeOfDay = (t < 0.5f) ? (phase & 3) : ((phase + 1) & 3);

#define MIX(f) RGB8 f = lerpRGB(A.f, B.f, t)
  MIX(skyTop); MIX(skyMid); MIX(skyHorizon); MIX(fog);
  MIX(grassL); MIX(grassD); MIX(roadL); MIX(roadD);
  MIX(rumbleL); MIX(rumbleD); MIX(lane); MIX(edgeLine);
  MIX(skylineFar); MIX(skylineNear); MIX(windowLit); MIX(windowDay);
  MIX(sun); MIX(sunGlow); MIX(cloud);
  MIX(tunnelWallL); MIX(tunnelWallD); MIX(tunnelCeil); MIX(tunnelLight); MIX(portal);
  MIX(foliageL); MIX(foliageD); MIX(trunk); MIX(rock);
  MIX(lampPole); MIX(lampLight);
#undef MIX

  grassL = mulRGB(grassL, T.grass);
  grassD = mulRGB(grassD, T.grass);
  fog = mulRGB(fog, T.fog);
  skylineFar = mulRGB(skylineFar, T.far);
  skylineNear = mulRGB(skylineNear, T.nearL);

  pal.skyTop = pack(skyTop); pal.skyMid = pack(skyMid);
  pal.skyHorizon = pack(skyHorizon); pal.fog = pack(fog);
  pal.grassL = pack(grassL); pal.grassD = pack(grassD);
  pal.roadL = pack(roadL); pal.roadD = pack(roadD);
  pal.rumbleL = pack(rumbleL); pal.rumbleD = pack(rumbleD);
  pal.lane = pack(lane); pal.edgeLine = pack(edgeLine);
  pal.skylineFar = pack(skylineFar); pal.skylineNear = pack(skylineNear);
  pal.windowLit = pack(windowLit); pal.windowDay = pack(windowDay);
  pal.sun = pack(sun); pal.sunGlow = pack(sunGlow); pal.cloud = pack(cloud);
  pal.tunnelWallL = pack(tunnelWallL); pal.tunnelWallD = pack(tunnelWallD);
  pal.tunnelCeil = pack(tunnelCeil); pal.tunnelLight = pack(tunnelLight);
  pal.portal = pack(portal);
  pal.foliageL = pack(foliageL); pal.foliageD = pack(foliageD);
  pal.trunk = pack(trunk); pal.rock = pack(rock);
  pal.lampPole = pack(lampPole); pal.lampLight = pack(lampLight);

  // Ground materials: the day colour times the road's day -> now ratio
  const RGB8 dayRoad = PALETTES[0].roadL;
  float ratio[3] = { roadL.r / (float)dayRoad.r, roadL.g / (float)dayRoad.g, roadL.b / (float)dayRoad.b };
  for (int m = 0; m < MAT_COUNT; m++) {
    for (int k = 0; k < 2; k++) {
      if (m == MAT_GRASS) pal.ground[m][k] = k ? pal.grassD : pal.grassL;
      else pal.ground[m][k] = pack(mulRGB(GROUND_DAY[m][k], ratio));
    }
  }
  pal.kerbL = pack(mulRGB(KERB_DAY[0], ratio));
  pal.kerbD = pack(mulRGB(KERB_DAY[1], ratio));

  float light[3], amb[3], dir[3];
  for (int i = 0; i < 3; i++) {
    light[i] = A.light[i] + (B.light[i] - A.light[i]) * t;
    amb[i]   = A.amb[i]   + (B.amb[i]   - A.amb[i])   * t;
    dir[i]   = A.sunDir[i] + (B.sunDir[i] - A.sunDir[i]) * t;
  }
  float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
  if (len < 1e-4f) len = 1;
  pal.lightR = light[0]; pal.lightG = light[1]; pal.lightB = light[2];
  pal.ambR = amb[0]; pal.ambG = amb[1]; pal.ambB = amb[2];
  pal.sunDirX = dir[0] / len; pal.sunDirY = dir[1] / len; pal.sunDirZ = dir[2] / len;
  pal.night = A.night + (B.night - A.night) * t;
  pal.sunY  = A.sunY  + (B.sunY  - A.sunY)  * t;
  pal.stars = A.stars + (B.stars - A.stars) * t;

  // The sun shines from +x when sunDirX > 0: left-side buildings show their
  // lit face to the road, right-side buildings their shaded face.
  for (int i = 0; i < BUILDING_PALETTE; i++) {
    pal.facadeLit[i]   = shadeRGB(FACADES[i], light, amb, 0.85f);
    pal.facadeShade[i] = shadeRGB(FACADES[i], light, amb, 0.30f);
    pal.facadeFront[i] = shadeRGB(FACADES[i], light, amb, 0.55f);
    pal.roof[i]        = shadeRGB(FACADES[i], light, amb, 0.70f);
  }
  sAppliedQ = floorf(cycle * 64.0f);
  sAppliedTheme = colorTheme;
}

void applyDayCycle(float c) {
  if (!(c == c)) c = 0;
  c = fmodf(c, 4.0f);
  if (c < 0) c += 4.0f;
  if (c >= 4.0f) c = 0.0f;
  // Rebuild only when the palette can actually change (blend window) or
  // every 1/64 of a phase: ~150 colour conversions, cheap but not free.
  float q = floorf(c * 64.0f);
  float frac = c - (int)c;
  if (frac > 1.0f - CYCLE_BLEND - 0.01f || q != sAppliedQ || sAppliedTheme != colorTheme) setDayCycle(c);
  else dayCycle = c;
}

void advanceDayCycle(float distance) {
  applyDayCycle(dayCycle + distance / CYCLE_PHASE_DIST);
}
