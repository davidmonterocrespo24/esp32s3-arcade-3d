/*
  ═══════════════════════════════════════════════════════════════
  SKY AND PARALLAX BACKGROUND
  The background is stored as height maps (~2 KB) and drawn every
  frame with the CURRENT palette, so the day/night cycle reaches it.
  Each circuit theme generates its own layers (deterministic Rng).
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "rendering.h"
#include "utils.h"
#include <math.h>

float skyOffset = 0.0f;

#define BG_W       640               // background wraps every 640 px
#define NUM_STARS  80
#define NUM_CLOUDS 7

static uint8_t sFarH[BG_W];          // far layer: hills / islands / ranges
static uint8_t sNearH[BG_W];         // near layer: skyline / ridges
static uint8_t sNearId[BG_W];        // building id per column (window pattern)
static uint8_t sSnow[BG_W];          // snow cap depth on the far layer (mountains)
struct Star  { uint16_t x; uint8_t y, b; };
struct Cloud { int16_t x; uint8_t y, w, h; };
static Star  sStars[NUM_STARS];
static Cloud sClouds[NUM_CLOUDS];
static int   sTheme = 0;

static void genSines(uint8_t* out, Rng& rng, float base, const float* amp, const int* freq,
                     int n, int lo, int hi) {
  float ph[6];
  for (int i = 0; i < n; i++) ph[i] = rng.frange(0, 6.2831853f);
  for (int x = 0; x < BG_W; x++) {
    float a = 2.0f * (float)PI * x / BG_W;
    float h = base;
    for (int i = 0; i < n; i++) h += amp[i] * sinf(a * freq[i] + ph[i]);
    out[x] = (uint8_t)iclamp((int)h, lo, hi);
  }
}

static void genSkyline(Rng& rng, int minH, int maxH, int lowChance) {
  int x = 0, id = 1;
  while (x < BG_W) {
    int w = rng.range(8, 26);
    int h = (rng.range(0, lowChance) == 0) ? rng.range(4, 10) : rng.range(minH, maxH);
    for (int i = 0; i < w && x + i < BG_W; i++) {
      sNearH[x + i] = (uint8_t)h;
      sNearId[x + i] = (uint8_t)id;
    }
    if (h > 34 && w > 10 && x + w / 2 < BG_W)
      sNearH[x + w / 2] = (uint8_t)(h + rng.range(4, 10));   // antenna
    x += w;
    id++;
  }
}

void initBackground(int theme) {
  sTheme = theme;
  Rng rng(0xB6000u + (uint32_t)theme * 7919u);
  for (int x = 0; x < BG_W; x++) { sFarH[x] = 0; sNearH[x] = 0; sNearId[x] = 0; sSnow[x] = 0; }

  if (theme == 1) {
    // COAST: open sea horizon with a few low islands far away
    int x = rng.range(0, 60);
    while (x < BG_W) {
      int w = rng.range(30, 90), h = rng.range(4, 13);
      for (int i = 0; i < w && x + i < BG_W; i++) {
        float t = (float)i / w;
        float s = sinf(t * (float)PI);
        sFarH[x + i] = (uint8_t)(h * s * (0.85f + 0.15f * sinf(t * 19.0f)));
      }
      x += w + rng.range(60, 200);
    }
    // a headland on one side of the panorama (the near layer, darker)
    int hx = rng.range(0, BG_W);
    for (int i = 0; i < 120; i++) {
      float t = (float)i / 120;
      int h = (int)(20 * sinf(t * (float)PI) * (0.8f + 0.2f * sinf(t * 23.0f)));
      sNearH[(hx + i) % BG_W] = (uint8_t)imax(0, h);
    }
  } else if (theme == 2) {
    // MOUNTAIN: high ranges with snow caps, darker forested ridges in front
    static const float A1[5] = { 16, 10, 6, 3, 1.5f };
    static const int   F1[5] = { 2, 5, 9, 17, 31 };
    genSines(sFarH, rng, 44, A1, F1, 5, 8, 80);
    for (int x = 0; x < BG_W; x++) {
      int h = sFarH[x];
      sSnow[x] = (uint8_t)(h > 40 ? imin((h - 40) / 2 + 2, 16) : 0);
    }
    static const float A2[4] = { 7, 4, 2, 1 };
    static const int   F2[4] = { 3, 7, 19, 43 };
    genSines(sNearH, rng, 18, A2, F2, 4, 4, 40);
  } else {
    // CITY: hazy distant towers behind a dense skyline with lit windows
    static const float A1[4] = { 9, 6, 3, 1.5f };
    static const int   F1[4] = { 2, 5, 11, 23 };
    genSines(sFarH, rng, 22, A1, F1, 4, 4, 60);
    // blocky distant towers on the far layer
    for (int x = 0; x < BG_W;) {
      int w = rng.range(6, 16), h = rng.range(18, 46);
      for (int i = 0; i < w && x + i < BG_W; i++) sFarH[x + i] = (uint8_t)imax(sFarH[x + i], h);
      x += w + rng.range(0, 10);
    }
    genSkyline(rng, 14, 52, 6);
  }

  for (int i = 0; i < NUM_STARS; i++) {
    sStars[i].x = (uint16_t)rng.range(0, BG_W);
    sStars[i].y = (uint8_t)rng.range(2, SCR_CY - 30);
    sStars[i].b = (uint8_t)rng.range(8, 33);
  }
  for (int i = 0; i < NUM_CLOUDS; i++) {
    sClouds[i].x = (int16_t)rng.range(0, BG_W);
    sClouds[i].y = (uint8_t)rng.range(12, 62);
    sClouds[i].w = (uint8_t)rng.range(22, 48);
    sClouds[i].h = (uint8_t)rng.range(4, 8);
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

  // 5. Far layer (hazy) and 6. near layer, column by column
  uint16_t farCol  = lerpCol(pal.skylineFar, pal.skyHorizon, sTheme == 1 ? 0.45f : 0.25f);
  uint16_t nearCol = pal.skylineNear;
  uint16_t snowCol = lerpCol(pal.ground[MAT_SNOW][0], pal.skyHorizon, 0.35f);
  int farOff  = (int)(skyOff * 0.5f);
  int nearOff = (int)skyOff;
  int litA = (sTheme == 0) ? (int)(pal.night * 30.0f) : 0;
  for (int x = 0; x < SCR_W; x++) {
    int fx = wrapBG(x + farOff);
    int fh = sFarH[fx];
    if (fh) {
      gfx_vspan(x, horizon - fh, horizon, farCol);
      if (sSnow[fx]) gfx_vspan(x, horizon - fh, horizon - fh + sSnow[fx], snowCol);
    }
    int wx = wrapBG(x + nearOff);
    int nh = sNearH[wx];
    if (!nh) continue;
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
