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

// ═══════════════════════════════════════════════════════════════
//  THEMES
//  Each circuit theme replaces the sky, the haze, the sun glow, the
//  clouds and the three background layers of the four base palettes
//  (blended with the same smoothstep, so the day/night cycle stays
//  smooth), and brings its own ground materials and plant tints.
// ═══════════════════════════════════════════════════════════════
struct ThemeSky {
  RGB8 skyTop, skyMid, skyHorizon, fog;
  RGB8 sunGlow, cloud;
  RGB8 far, mid, nearL;          // background layers, far -> near
};

// [theme][phase]: phase 0 day, 1 sunset, 2 night, 3 dawn
static const ThemeSky THEME_SKY[3][4] = {
  { // NEON CITY: urban haze by day, magenta dusk, purple light-polluted night
    { {64, 124, 214}, {132, 174, 232}, {206, 220, 238}, {190, 198, 214},
      {255, 236, 170}, {255, 255, 255}, {156, 170, 196}, {118, 130, 160}, {78, 88, 112} },
    { {34, 24, 84}, {168, 60, 120}, {255, 138, 92}, {214, 120, 118},
      {255, 104, 72}, {255, 140, 130}, {150, 82, 124}, {98, 48, 98}, {52, 28, 68} },
    { {4, 4, 20}, {20, 16, 54}, {74, 40, 96}, {40, 28, 66},
      {110, 120, 200}, {58, 46, 92}, {38, 30, 72}, {24, 18, 50}, {12, 10, 30} },
    { {60, 78, 150}, {196, 146, 178}, {255, 198, 160}, {226, 188, 180},
      {255, 180, 150}, {255, 212, 212}, {170, 150, 180}, {124, 108, 146}, {84, 74, 108} },
  },
  { // SUNSET COAST: deep Mediterranean blue, golden sunset, clear night
    { {30, 104, 214}, {92, 166, 238}, {204, 230, 250}, {196, 222, 240},
      {255, 240, 180}, {255, 255, 255}, {136, 164, 196}, {100, 132, 130}, {72, 104, 86} },
    { {44, 48, 118}, {214, 106, 96}, {255, 184, 92}, {246, 166, 112},
      {255, 122, 48}, {255, 166, 120}, {170, 106, 124}, {120, 72, 90}, {80, 48, 64} },
    { {4, 8, 28}, {14, 24, 62}, {38, 48, 94}, {26, 34, 64},
      {110, 122, 200}, {40, 46, 82}, {30, 36, 64}, {20, 26, 48}, {12, 16, 32} },
    { {62, 82, 152}, {202, 150, 180}, {255, 202, 162}, {232, 194, 182},
      {255, 180, 150}, {255, 212, 212}, {172, 150, 184}, {126, 110, 140}, {86, 80, 104} },
  },
  { // ALPINE PASS: cold violet dawn, crisp blue day, blue ranges
    { {24, 84, 204}, {96, 156, 238}, {196, 222, 250}, {198, 216, 236},
      {255, 246, 200}, {255, 255, 255}, {150, 170, 208}, {100, 122, 160}, {44, 76, 64} },
    { {40, 36, 104}, {190, 90, 120}, {255, 164, 92}, {232, 150, 110},
      {255, 112, 60}, {255, 150, 130}, {156, 98, 128}, {104, 64, 100}, {46, 40, 54} },
    { {2, 6, 22}, {10, 18, 50}, {34, 42, 84}, {22, 28, 56},
      {110, 122, 200}, {34, 40, 72}, {30, 36, 66}, {20, 26, 48}, {8, 14, 20} },
    { {52, 70, 150}, {180, 140, 190}, {255, 194, 170}, {218, 190, 200},
      {255, 170, 150}, {255, 206, 214}, {168, 140, 186}, {118, 100, 146}, {50, 56, 72} },
  },
};

// Ground materials in daylight (light band, dark band). Other times of day
// are derived with the road's day -> phase ratio, so they darken and tint
// exactly like the tarmac next to them. MAT_GRASS uses the base palette's
// hand-tuned grass times the theme tint; water also mirrors the sky.
struct ThemeGround {
  RGB8 mat[MAT_COUNT][2];
  RGB8 kerb[2];
  float grass[3], foliage[3];
  float waterSky;                // how much the water mirrors the horizon (0..1)
};

static const ThemeGround THEME_GROUND[3] = {
  { // NEON CITY: grey slabs, a green harbour basin, lush park grass
    { { {58, 150, 56}, {44, 128, 44} }, { {176, 172, 166}, {160, 156, 150} },
      { {220, 200, 150}, {208, 188, 140} }, { {40, 98, 124}, {34, 86, 112} },
      { {128, 124, 118}, {116, 112, 106} }, { {240, 244, 252}, {224, 230, 242} },
      { {140, 112, 80}, {128, 102, 72} } },
    { {214, 212, 206}, {124, 122, 118} },
    { 0.90f, 0.98f, 0.90f }, { 0.95f, 1.00f, 0.95f }, 0.38f },
  { // SUNSET COAST: golden sand, turquoise sea, dry grass, warm limestone
    { { {124, 158, 62}, {106, 140, 52} }, { {216, 200, 178}, {200, 184, 162} },
      { {238, 216, 162}, {226, 204, 150} }, { {30, 136, 178}, {26, 120, 162} },
      { {178, 152, 120}, {162, 138, 108} }, { {240, 244, 252}, {224, 230, 242} },
      { {170, 130, 86}, {156, 118, 78} } },
    { {232, 228, 220}, {176, 92, 70} },
    { 1.12f, 1.00f, 0.70f }, { 1.08f, 1.00f, 0.72f }, 0.30f },
  { // ALPINE PASS: alpine meadow, granite, blue-white snow, a dark lake
    { { {62, 140, 58}, {48, 120, 48} }, { {170, 168, 162}, {156, 154, 148} },
      { {220, 200, 150}, {208, 188, 140} }, { {36, 100, 134}, {30, 88, 120} },
      { {130, 130, 134}, {116, 116, 122} }, { {244, 248, 255}, {226, 234, 248} },
      { {128, 100, 72}, {116, 90, 64} } },
    { {214, 212, 206}, {124, 122, 118} },
    { 0.80f, 0.96f, 0.86f }, { 0.78f, 0.92f, 0.86f }, 0.42f },
};

// Prop materials (PropColor), lit like the facades
static const RGB8 PROPS[PROP_PALETTE] = {
  {236, 236, 230}, // white paint
  {204, 40, 36},   // red
  {250, 196, 30},  // yellow
  {28, 28, 32},    // black
  {156, 160, 166}, // metal
  {66, 70, 78},    // dark metal
  {156, 104, 62},  // wood
  {92, 60, 38},    // dark wood
  {168, 166, 158}, // concrete
  {118, 158, 188}, // glass
  {242, 120, 28},  // orange
  {36, 88, 200},   // blue
  {40, 150, 70},   // green
  {150, 140, 126}, // stone
  {222, 202, 150}, // sand
  {150, 70, 190}   // purple
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

  const int pa = phase & 3, pb = (phase + 1) & 3;
  const Palette& A = PALETTES[pa];
  const Palette& B = PALETTES[pb];
  const int th = (colorTheme >= 0 && colorTheme < 3) ? colorTheme : 0;
  const ThemeSky& SA = THEME_SKY[th][pa];
  const ThemeSky& SB = THEME_SKY[th][pb];
  const ThemeGround& G = THEME_GROUND[th];
  timeOfDay = (t < 0.5f) ? pa : pb;

#define MIX(f) RGB8 f = lerpRGB(A.f, B.f, t)
#define TMIX(f, src) RGB8 f = lerpRGB(SA.src, SB.src, t)
  TMIX(skyTop, skyTop); TMIX(skyMid, skyMid); TMIX(skyHorizon, skyHorizon); TMIX(fog, fog);
  MIX(grassL); MIX(grassD); MIX(roadL); MIX(roadD);
  MIX(rumbleL); MIX(rumbleD); MIX(lane); MIX(edgeLine);
  TMIX(skylineFar, far); TMIX(skylineMid, mid); TMIX(skylineNear, nearL);
  MIX(windowLit); MIX(windowDay);
  MIX(sun); TMIX(sunGlow, sunGlow); TMIX(cloud, cloud);
  MIX(tunnelWallL); MIX(tunnelWallD); MIX(tunnelCeil); MIX(tunnelLight); MIX(portal);
  MIX(foliageL); MIX(foliageD); MIX(trunk); MIX(rock);
  MIX(lampPole); MIX(lampLight);
#undef MIX
#undef TMIX

  grassL = mulRGB(grassL, G.grass);
  grassD = mulRGB(grassD, G.grass);
  foliageL = mulRGB(foliageL, G.foliage);
  foliageD = mulRGB(foliageD, G.foliage);

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

  // Ground materials: the theme's day colour times the road's day -> now
  // ratio; the water also mirrors the horizon
  const RGB8 dayRoad = PALETTES[0].roadL;
  float ratio[3] = { roadL.r / (float)dayRoad.r, roadL.g / (float)dayRoad.g, roadL.b / (float)dayRoad.b };
  RGB8 waterC[2];
  for (int m = 0; m < MAT_COUNT; m++) {
    for (int k = 0; k < 2; k++) {
      if (m == MAT_GRASS) { pal.ground[m][k] = k ? pal.grassD : pal.grassL; continue; }
      RGB8 c = mulRGB(G.mat[m][k], ratio);
      if (m == MAT_WATER) { c = lerpRGB(c, skyHorizon, G.waterSky * (k ? 0.8f : 1.0f)); waterC[k] = c; }
      pal.ground[m][k] = pack(c);
    }
  }
  pal.kerbL = pack(mulRGB(G.kerb[0], ratio));
  pal.kerbD = pack(mulRGB(G.kerb[1], ratio));
  // the open sea of the background: deeper near the land, hazier far out
  pal.water = pack(lerpRGB(waterC[0], skyHorizon, 0.30f));
  pal.waterDeep = pack(lerpRGB(waterC[1], skyTop, 0.12f));
  pal.skylineMid = pack(skylineMid);

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
  for (int i = 0; i < PROP_PALETTE; i++) pal.prop[i] = shadeRGB(PROPS[i], light, amb, 0.75f);
  pal.seaGlint = lerpCol(pal.sunGlow, COL_WHITE, 0.35f);
  // snow on the distant ranges: lit by the sun / moon, a little hazy
  pal.snowCap = lerpCol(shadeRGB(G.mat[MAT_SNOW][0], light, amb, 0.80f), pal.skyHorizon, 0.25f);
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
