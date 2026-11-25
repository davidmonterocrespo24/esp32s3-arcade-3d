/*
  ═══════════════════════════════════════════════════════════════
  SKY AND PARALLAX BACKGROUND
  The old version baked a fixed sunset into a 640x120 PSRAM sprite
  (150 KB) and copied it every frame, so the day/night palette never
  reached the sky. Now the background is stored as height maps
  (~2 KB) and drawn every frame with the CURRENT palette.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "rendering.h"
#include "utils.h"
#include <Arduino.h>
#include <math.h>

float skyOffset = 0.0f;

#define BG_W       640               // background wraps every 640 px
#define NUM_STARS  80
#define NUM_CLOUDS 7

static uint8_t sFarH[BG_W];          // distant hills / mountain ridge
static uint8_t sNearH[BG_W];         // city skyline
static uint8_t sNearId[BG_W];        // building id per column (window pattern)
struct Star  { uint16_t x; uint8_t y, b; };
struct Cloud { int16_t x; uint8_t y, w, h; };
static Star  sStars[NUM_STARS];
static Cloud sClouds[NUM_CLOUDS];

void initBackground() {
  // Far ridge: a few octaves of sines with random phases, wrapping at BG_W
  float ph[4];
  for (int i = 0; i < 4; i++) ph[i] = random(0, 6283) / 1000.0f;
  for (int x = 0; x < BG_W; x++) {
    float a = 2.0f * (float)PI * x / BG_W;
    float h = 22.0f + 9.0f * sinf(a * 2 + ph[0]) + 6.0f * sinf(a * 5 + ph[1])
            + 3.0f * sinf(a * 11 + ph[2]) + 1.5f * sinf(a * 23 + ph[3]);
    sFarH[x] = (uint8_t)iclamp((int)h, 4, 60);
  }

  // City skyline: blocks of random width/height, a few gaps, antennas
  int x = 0, id = 1;
  while (x < BG_W) {
    int w = random(8, 26);
    int h = (random(0, 6) == 0) ? random(4, 10) : random(14, 52);
    for (int i = 0; i < w && x + i < BG_W; i++) {
      sNearH[x + i] = (uint8_t)h;
      sNearId[x + i] = (uint8_t)id;
    }
    if (h > 34 && w > 10) sNearH[x + w / 2] = (uint8_t)(h + random(4, 10));   // antenna
    x += w;
    id++;
  }

  for (int i = 0; i < NUM_STARS; i++) {
    sStars[i].x = (uint16_t)random(0, BG_W);
    sStars[i].y = (uint8_t)random(2, SCR_CY - 30);
    sStars[i].b = (uint8_t)random(8, 33);
  }
  for (int i = 0; i < NUM_CLOUDS; i++) {
    sClouds[i].x = (int16_t)random(0, BG_W);
    sClouds[i].y = (uint8_t)random(12, 62);
    sClouds[i].w = (uint8_t)random(22, 48);
    sClouds[i].h = (uint8_t)random(4, 8);
  }
}

static inline void unpack(uint16_t c, float& r, float& g, float& b) {
  r = ((c >> 11) & 31) * (255.0f / 31.0f);
  g = ((c >> 5) & 63) * (255.0f / 63.0f);
  b = (c & 31) * (255.0f / 31.0f);
}

static inline int wrapBG(int x) {
  x %= BG_W;
  return x < 0 ? x + BG_W : x;
}

void drawSky(float skyOff, float timeSec) {
  const int horizon = SCR_CY;

  // 1. Gradient: top -> mid (55%) -> horizon, ordered-dithered (no banding)
  float tr, tg, tb, mr, mg, mb, hr, hg, hb;
  unpack(pal.skyTop, tr, tg, tb);
  unpack(pal.skyMid, mr, mg, mb);
  unpack(pal.skyHorizon, hr, hg, hb);
  auto skyAt = [&](int y, float& r, float& g, float& b) {
    float t = (float)y / horizon;
    if (t < 0.55f) {
      float k = t / 0.55f;
      r = tr + (mr - tr) * k; g = tg + (mg - tg) * k; b = tb + (mb - tb) * k;
    } else {
      float k = (t - 0.55f) / 0.45f;
      k = k * k * (3 - 2 * k);
      r = mr + (hr - mr) * k; g = mg + (hg - mg) * k; b = mb + (hb - mb) * k;
    }
  };
  for (int y = 0; y < horizon; y++) {
    float r, g, b;
    skyAt(y, r, g, b);
    gfx_gradientRow(y, 0, SCR_W, r, g, b);
  }

  // 2. Stars (fade in with the night, twinkle)
  if (pal.stars > 0.02f) {
    int off = (int)(skyOff * 0.15f);
    uint32_t tick = (uint32_t)(timeSec * 6.0f);
    for (int i = 0; i < NUM_STARS; i++) {
      int sx = wrapBG(sStars[i].x - off);
      if (sx >= SCR_W) continue;
      uint32_t tw = hash32(i * 977u + tick) & 7;
      int a = (int)(sStars[i].b * pal.stars) - (int)(tw == 0 ? 10 : 0);
      if (a <= 0) continue;
      gfx_pixelMode(sx, sStars[i].y, COL_WHITE, GFX_BLEND, a > 32 ? 32 : a);
      if (sStars[i].b > 28 && a > 20) {   // a few bright ones get a cross
        gfx_pixelMode(sx - 1, sStars[i].y, COL_WHITE, GFX_BLEND, a / 3);
        gfx_pixelMode(sx + 1, sStars[i].y, COL_WHITE, GFX_BLEND, a / 3);
      }
    }
  }

  // 3. Sun / moon (slow parallax), with an additive glow
  {
    int sx = wrapBG(SCR_W - (int)(skyOff * 0.25f) - 40) - BG_W / 4;
    int sy = (int)pal.sunY;
    if (sx > -80 && sx < SCR_W + 80) {
      float night = pal.night;
      int glowR = (int)(70 - 30 * night);
      gfx_glow(sx, sy, glowR, pal.sunGlow, (int)(26 - 10 * night));
      int r = (int)(13 - 4 * night);
      gfx_fillCircle(sx, sy, r, pal.sun);
      if (night > 0.6f) {               // crescent: bite out with the local sky colour
        float br, bg, bb;
        skyAt(sy - r / 3, br, bg, bb);
        uint16_t bite = rgb((uint8_t)br, (uint8_t)bg, (uint8_t)bb);
        gfx_fillCircle(sx + r / 2 + 1, sy - r / 3, r - 1, bite);
      }
    }
  }

  // 4. Clouds (soft, blended; dim and tinted by the palette)
  {
    int a = (int)(14 - 8 * pal.night);
    int off = (int)(skyOff * 0.35f + timeSec * 2.0f);
    for (int i = 0; i < NUM_CLOUDS; i++) {
      const Cloud& c = sClouds[i];
      int cx = wrapBG(c.x - off) - 40;
      if (cx < -60 || cx > SCR_W + 60) continue;
      gfx_fillEllipse(cx, c.y, c.w, c.h, pal.cloud, GFX_BLEND, a);
      gfx_fillEllipse(cx - c.w / 3, c.y + 1, c.w / 2, c.h - 1, pal.cloud, GFX_BLEND, a);
      gfx_fillEllipse(cx + c.w / 4, c.y - c.h / 2, c.w / 2, c.h, pal.cloud, GFX_BLEND, a + 4);
    }
  }

  // 5. Far ridge (hazy) and 6. city skyline, column by column
  uint16_t farCol  = lerpCol(pal.skylineFar, pal.skyHorizon, 0.25f);
  uint16_t nearCol = pal.skylineNear;
  int farOff  = (int)(skyOff * 0.5f);
  int nearOff = (int)skyOff;
  int litA = (int)(pal.night * 30.0f);
  for (int x = 0; x < SCR_W; x++) {
    int fh = sFarH[wrapBG(x + farOff)];
    gfx_vspan(x, horizon - fh, horizon, farCol);
    int wx = wrapBG(x + nearOff);
    int nh = sNearH[wx];
    gfx_vspan(x, horizon - nh, horizon, nearCol);
    // Windows: every 3rd column inside a building, every 3rd row
    if (litA > 2 && nh > 8 && (wx % 3) == 1) {
      uint32_t id = sNearId[wx];
      for (int y = horizon - nh + 3; y < horizon - 2; y += 3) {
        if ((hash32(id * 131u + (uint32_t)wx * 7u + (uint32_t)y) & 3) == 0)
          gfx_pixelMode(x, y, pal.windowLit, GFX_BLEND, litA);
      }
    }
  }

  // 7. Horizon haze over the skyline base
  for (int i = 0; i < 10; i++) {
    int y = horizon - 10 + i;
    gfx_hspanMode(y, 0, SCR_W, pal.fog, GFX_BLEND, 2 + i * 2);
  }
}
