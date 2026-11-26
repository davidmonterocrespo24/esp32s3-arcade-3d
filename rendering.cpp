/*
  ═══════════════════════════════════════════════════════════════
  SKY AND PARALLAX BACKGROUND
  The background is stored as height maps (~3.3 KB) and drawn every
  frame with the CURRENT palette, so the day/night cycle reaches it.
  Each circuit theme generates its own layers (deterministic Rng):

    CITY      three skyline layers (hazy far towers, tall mid towers
              with blinking antenna lights, dense near blocks), a TV
              tower, windows lit one by one at night, stratus streaks
    COAST     the open sea up to the horizon with the sun's glitter
              path, distant islands, two headlands, cumulus clouds
    MOUNTAIN  three ranges: high snow-capped peaks, a darker middle
              range, a forested near ridge with pine tips; slopes lit
              from the sun's side, haze and mist between the layers

  Layers are drawn column by column without overdraw (each column paints
  only the part of a layer that the nearer layers leave visible).
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "rendering.h"
#include "utils.h"
#include <math.h>

float skyOffset = 0.0f;
float bgTime = 0.0f;

#define BG_W        640              // background wraps every 640 px
#define NUM_STARS   80
#define NUM_CLOUDS  8
#define NUM_ANTENNA 10
#define SEA_H       7                // coast: rows of open sea above the camera horizon

static uint8_t sFarH[BG_W];          // far layer: towers / islands / high ranges
static uint8_t sMidH[BG_W];          // mid layer: tall towers / headlands / middle range
static uint8_t sNearH[BG_W];         // near layer: dense blocks / - / forested ridge
static uint8_t sNearId[BG_W];        // city: building id per column (window pattern)
static uint8_t sMidId[BG_W];
static uint8_t sSnow[BG_W];          // mountain: snow depth on the far / mid layer (low / high nibble)
struct Star    { uint16_t x; uint8_t y, b; };
struct Cloud   { int16_t x; uint8_t y, w, h, puffs; };
struct Antenna { int16_t x; uint8_t h; uint8_t phase; };
static Star    sStars[NUM_STARS];
static Cloud   sClouds[NUM_CLOUDS];
static Antenna sAnt[NUM_ANTENNA];
static int     sAntN = 0;
static int     sTheme = 0;
static int     sTowerX = 420;        // city TV tower (mid-layer panorama x)

static inline int wrapBG(int x) {
  x %= BG_W;
  return x < 0 ? x + BG_W : x;
}

// Sum of sines; the first `ridged` octaves use 1 - 2|sin| (sharp crests and
// rounded valleys: mountain peaks), the others plain sines (small bumps).
// Integer frequencies keep the panorama seamless at the wrap.
static void genSines(uint8_t* out, Rng& rng, float base, const float* amp, const int* freq,
                     int n, int lo, int hi, int ridged = 0) {
  float ph[6];
  for (int i = 0; i < n; i++) ph[i] = rng.frange(0, 6.2831853f);
  for (int x = 0; x < BG_W; x++) {
    float a = 2.0f * (float)PI * x / BG_W;
    float h = base;
    for (int i = 0; i < n; i++) {
      float s = sinf(a * freq[i] + ph[i]);
      h += (i < ridged) ? amp[i] * (1.0f - 2.0f * fabsf(s)) : amp[i] * s;
    }
    out[x] = (uint8_t)iclamp((int)h, lo, hi);
  }
}

// Which way each column of a layer faces (bit set = the slope faces right),
// from a wide stencil so small bumps do not flicker between lit and shaded.
static uint8_t sFacing[BG_W];        // bit 0 far layer, bit 1 mid layer
static void computeFacing(const uint8_t* h, uint8_t bit) {
  for (int x = 0; x < BG_W; x++) {
    int s = (int)h[(x + 6) % BG_W] - (int)h[(x + BG_W - 6) % BG_W];
    if (s < 0) sFacing[x] |= bit; else sFacing[x] &= (uint8_t)~bit;
  }
}
static inline bool slopeLit(int x, uint8_t bit, bool sunRight) {
  return ((sFacing[x] & bit) != 0) == sunRight;
}

// Blocky skyline into h/id: widths w0..w1, heights h0..h1, a few low gaps,
// optional setback tops; tall ones may get an antenna
static void genBlocks(uint8_t* h, uint8_t* id, Rng& rng, int w0, int w1, int h0, int h1, int lowChance,
                      bool antennas) {
  int x = 0, n = 1;
  while (x < BG_W) {
    int w = rng.range(w0, w1 + 1);
    int hh = (rng.range(0, lowChance) == 0) ? rng.range(h0 / 3, h0) : rng.range(h0, h1 + 1);
    bool setback = hh > (h0 + h1) / 2 && w > 9 && rng.chance(0.5f);
    for (int i = 0; i < w && x + i < BG_W; i++) {
      int v = hh;
      if (setback && (i < w / 4 || i >= w - w / 4)) v = hh - hh / 6;   // stepped top
      h[x + i] = (uint8_t)imax(h[x + i], v);
      if (id) id[x + i] = (uint8_t)n;
    }
    if (antennas && hh > h1 - (h1 - h0) / 4 && sAntN < NUM_ANTENNA && x + w / 2 < BG_W) {
      sAnt[sAntN].x = (int16_t)(x + w / 2);
      sAnt[sAntN].h = (uint8_t)rng.range(5, 11);          // mast above the roof
      sAnt[sAntN].phase = (uint8_t)rng.range(0, 255);
      sAntN++;
    }
    x += w;
    n++;
  }
}

// A smooth hump (island / headland) into h: base width w, peak height ph
static void hump(uint8_t* h, int x0, int w, int ph, float rough, Rng& rng) {
  float p1 = rng.frange(0, 6.28f), p2 = rng.frange(0, 6.28f);
  for (int i = 0; i < w; i++) {
    float t = (float)i / w;
    float s = sinf(t * (float)PI);
    s = s * s * (3 - 2 * s);                                // fuller shoulders
    float v = ph * s * (1.0f + rough * (0.6f * sinf(t * 17.0f + p1) + 0.4f * sinf(t * 41.0f + p2)));
    int x = wrapBG(x0 + i);
    h[x] = (uint8_t)imax(h[x], iclamp((int)v, 0, 120));
  }
}

void initBackground(int theme) {
  sTheme = theme;
  Rng rng(0xB6000u + (uint32_t)theme * 7919u);
  for (int x = 0; x < BG_W; x++) {
    sFarH[x] = sMidH[x] = sNearH[x] = 0;
    sNearId[x] = sMidId[x] = 0;
    sSnow[x] = 0;
  }
  sAntN = 0;

  if (theme == 1) {
    // COAST: islands far out, two headlands nearer, nothing in front
    int x = rng.range(0, 40);
    while (x < BG_W) {
      int w = rng.range(26, 80);
      hump(sFarH, x, w, rng.range(4, 12), 0.15f, rng);
      x += w + rng.range(50, 170);
    }
    hump(sMidH, rng.range(0, 80), 150, 24, 0.12f, rng);           // a long headland
    hump(sMidH, 330 + rng.range(0, 60), 110, 17, 0.18f, rng);     // a rocky cape
  } else if (theme == 2) {
    // MOUNTAIN: sharp high peaks, a middle range, a forested ridge
    static const float A1[5] = { 16, 12, 7, 2.5f, 1.0f };
    static const int   F1[5] = { 2, 5, 9, 17, 31 };
    genSines(sFarH, rng, 52, A1, F1, 5, 12, 94, 3);
    static const float A2[4] = { 9, 6, 2.5f, 1.0f };
    static const int   F2[4] = { 3, 7, 13, 29 };
    genSines(sMidH, rng, 30, A2, F2, 4, 6, 62, 2);
    static const float A3[4] = { 5, 3, 1.5f, 0.8f };
    static const int   F3[4] = { 4, 9, 21, 47 };
    genSines(sNearH, rng, 13, A3, F3, 4, 3, 26);
    for (int i = 0; i < BG_W; i++) {
      // pine tips along the near ridge: a sawtooth with a little jitter
      int k = i % 5;
      int tip = (k == 2) ? 3 : ((k == 1 || k == 3) ? 1 : 0);
      if ((hash32((uint32_t)i * 2654435761u) & 7) == 0) tip = 0;
      sNearH[i] = (uint8_t)imin(255, sNearH[i] + tip);
      // snow: on the far range above 58 px, on the middle range above 44 px,
      // the snow line wavy (low-frequency) rather than a straight cut
      float a = 2.0f * (float)PI * i / BG_W;
      int jag = (int)(2.0f + 1.5f * sinf(a * 37.0f) + 1.0f * sinf(a * 83.0f + 1.3f));
      int sf = sFarH[i] > 58 ? imin(15, (sFarH[i] - 58) / 2 + 2 + jag) : 0;
      int sm = sMidH[i] > 44 ? imin(15, (sMidH[i] - 44) / 2 + 1 + jag / 2) : 0;
      sSnow[i] = (uint8_t)(sf | (sm << 4));
    }
  } else {
    // CITY: hazy far towers, tall mid towers with antennas, dense near blocks
    static const float A1[3] = { 6, 3, 1.5f };
    static const int   F1[3] = { 2, 5, 11 };
    genSines(sFarH, rng, 14, A1, F1, 3, 4, 30);
    genBlocks(sFarH, nullptr, rng, 5, 12, 18, 44, 5, false);
    genBlocks(sMidH, sMidId, rng, 7, 16, 26, 66, 4, true);
    genBlocks(sNearH, sNearId, rng, 9, 22, 10, 30, 5, false);
    // keep the TV tower's neighbourhood low so it stands out
    sTowerX = 400 + rng.range(0, 80);
    for (int i = -10; i <= 10; i++) {
      int x = wrapBG(sTowerX + i);
      sMidH[x] = (uint8_t)imin(sMidH[x], 30);
    }
  }

  computeFacing(sFarH, 1);
  computeFacing(sMidH, 2);

  for (int i = 0; i < NUM_STARS; i++) {
    sStars[i].x = (uint16_t)rng.range(0, BG_W);
    sStars[i].y = (uint8_t)rng.range(2, SCR_CY - 30);
    sStars[i].b = (uint8_t)rng.range(8, 33);
  }
  for (int i = 0; i < NUM_CLOUDS; i++) {
    Cloud& c = sClouds[i];
    c.x = (int16_t)(i * (BG_W / NUM_CLOUDS) + rng.range(0, BG_W / NUM_CLOUDS));
    if (theme == 0) {          // stratus streaks
      c.y = (uint8_t)rng.range(14, 70); c.w = (uint8_t)rng.range(36, 90); c.h = (uint8_t)rng.range(2, 4);
      c.puffs = 1;
    } else if (theme == 1) {   // cumulus
      c.y = (uint8_t)rng.range(22, 72); c.w = (uint8_t)rng.range(22, 44); c.h = (uint8_t)rng.range(7, 12);
      c.puffs = (uint8_t)rng.range(3, 6);
    } else {                   // small clouds round the peaks
      c.y = (uint8_t)rng.range(10, 52); c.w = (uint8_t)rng.range(20, 46); c.h = (uint8_t)rng.range(3, 6);
      c.puffs = (uint8_t)rng.range(1, 3);
    }
  }
}

static inline void unpack(uint16_t c, float& r, float& g, float& b) {
  r = ((c >> 11) & 31) * (255.0f / 31.0f);
  g = ((c >> 5) & 63) * (255.0f / 63.0f);
  b = (c & 31) * (255.0f / 31.0f);
}

// ═══════════════════════════════════════════════════════════════
//  CLOUDS
// ═══════════════════════════════════════════════════════════════
static void drawClouds(float skyOff, float timeSec) {
  const float n = pal.night;
  int a = (int)(14 - 7 * n);
  const int off = (int)(skyOff * 0.35f + timeSec * 2.0f);
  const uint16_t lit = pal.cloud;
  const uint16_t base = lerpCol(pal.cloud, pal.skyMid, 0.45f);     // darker underside
  for (int i = 0; i < NUM_CLOUDS; i++) {
    const Cloud& c = sClouds[i];
    int cx = wrapBG(c.x - off) - 60;
    if (cx < -100 || cx > SCR_W + 100) continue;
    if (sTheme == 0) {             // thin streaks lit from below at dusk
      gfx_fillEllipse(cx, c.y, c.w, c.h, lit, GFX_BLEND, a);
      gfx_fillEllipse(cx + c.w / 5, c.y + 1, c.w / 2, imax(1, c.h - 1), base, GFX_BLEND, a / 2 + 2);
    } else if (sTheme == 1) {      // puffy cumulus: a flat shaded base and round lit tops
      gfx_fillEllipse(cx, c.y + c.h / 2, c.w, imax(2, c.h / 2), base, GFX_BLEND, a + 6);
      for (int p = 0; p < c.puffs; p++) {
        int px = cx - c.w + (2 * c.w) * (p + 1) / (c.puffs + 1);
        int r = c.h + (int)((hash32((uint32_t)(i * 7 + p)) % 5));
        if (p == c.puffs / 2) r += 3;
        gfx_fillEllipse(px, c.y + c.h / 2 - r / 2, r, r * 3 / 4 + 1, lit, GFX_BLEND, a + 10);
      }
    } else {                       // lenticular wisps
      gfx_fillEllipse(cx, c.y, c.w, c.h, lit, GFX_BLEND, a);
      if (c.puffs > 1) gfx_fillEllipse(cx + c.w / 3, c.y - c.h / 2, c.w / 2, imax(1, c.h / 2), lit, GFX_BLEND, a);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  LAYERS PER THEME
// ═══════════════════════════════════════════════════════════════
static void drawCityLayers(float skyOff, float timeSec) {
  const int horizon = SCR_CY;
  const float n = pal.night;
  const uint16_t farCol  = lerpCol(pal.skylineFar, pal.skyHorizon, 0.30f);
  const uint16_t midCol  = pal.skylineMid;
  const uint16_t nearCol = pal.skylineNear;
  // window colours: warm and cool lights, fading in with the night
  const uint16_t winW = lerpCol(midCol, pal.windowLit, 0.15f + 0.75f * n);
  const uint16_t winC = lerpCol(midCol, rgb(200, 220, 255), 0.10f + 0.60f * n);
  const uint16_t winN = lerpCol(nearCol, pal.windowLit, 0.15f + 0.80f * n);
  const bool windows = n > 0.05f;
  const int farOff = (int)(skyOff * 0.35f), midOff = (int)(skyOff * 0.6f), nearOff = (int)skyOff;
  for (int x = 0; x < SCR_W; x++) {
    const int fx = wrapBG(x + farOff), mx = wrapBG(x + midOff), nx = wrapBG(x + nearOff);
    const int fh = sFarH[fx], mh = sMidH[mx], nh = sNearH[nx];
    int bottom = horizon;
    if (nh) { gfx_vspan(x, horizon - nh, bottom, nearCol); bottom = horizon - nh; }
    if (mh > nh) { gfx_vspan(x, horizon - mh, bottom, midCol); bottom = horizon - mh; }
    if (fh > imax(mh, nh)) gfx_vspan(x, horizon - fh, bottom, farCol);
    if (!windows) continue;
    // windows on the mid towers: every other column, every third row
    if (mh > nh + 4 && (mx & 1) == 0) {
      const uint32_t id = sMidId[mx];
      for (int y = horizon - mh + 3; y < horizon - nh - 1; y += 3) {
        uint32_t h = hash32(id * 131u + (uint32_t)mx * 7u + (uint32_t)y * 13u);
        if ((h & 3) == 0) gfx_pixel(x, y, (h & 64) ? winC : winW);
      }
    }
    // windows on the near blocks: denser and warmer
    if (nh > 6 && (nx % 3) != 2) {
      const uint32_t id = sNearId[nx];
      for (int y = horizon - nh + 2; y < horizon - 1; y += 3) {
        uint32_t h = hash32(id * 977u + (uint32_t)nx * 3u + (uint32_t)y * 31u);
        if ((h % 5) < 2) gfx_pixel(x, y, winN);
      }
    }
  }
  // TV tower (on the mid layer): shaft, pod, mast, beacon
  {
    int tx = wrapBG(sTowerX - midOff);
    if (tx > SCR_W) tx -= BG_W;
    if (tx > -12 && tx < SCR_W + 12) {
      const uint16_t c = lerpCol(midCol, pal.skyHorizon, 0.08f);
      gfx_fillRect(tx - 1, horizon - 82, 3, 82, c);
      gfx_fillRect(tx - 3, horizon - 26, 7, 26, c);                 // flared base
      gfx_fillEllipse(tx, horizon - 70, 7, 4, c);                    // the pod
      gfx_fillRect(tx - 4, horizon - 76, 9, 2, c);
      gfx_vspan(tx, horizon - 98, horizon - 82, c);                  // mast
      if (n > 0.05f) {
        uint16_t ring = lerpCol(c, pal.windowLit, 0.2f + 0.7f * n);
        for (int k = -5; k <= 5; k += 2) gfx_pixel(tx + k, horizon - 70, ring);
      }
      if (n > 0.2f && fmodf(timeSec, 1.6f) < 0.6f) {
        gfx_pixel(tx, horizon - 98, COL_RED);
        gfx_glow(tx, horizon - 98, 4, rgb(255, 30, 20), (int)(26 * n));
      }
    }
  }
  // masts on the tallest mid towers, with blinking red aircraft lights at night
  for (int a = 0; a < sAntN; a++) {
    const int px = sAnt[a].x;
    const int roof = sMidH[px];
    int ax = wrapBG(px - midOff);
    if (ax >= SCR_W) continue;
    if (roof < 34 || roof <= sNearH[wrapBG(ax + nearOff)]) continue;
    int ay = horizon - roof - sAnt[a].h;
    gfx_vspan(ax, ay, horizon - roof, midCol);
    float ph = timeSec + sAnt[a].phase * 0.01f;
    if (n > 0.2f && fmodf(ph, 1.2f) < 0.45f) {
      gfx_pixel(ax, ay, rgb(255, 40, 30));
      gfx_glow(ax, ay, 3, rgb(255, 30, 20), (int)(20 * n));
    }
  }
}

static void drawCoastLayers(float skyOff, float timeSec, int sunX) {
  const int horizon = SCR_CY;
  const int seaTop = horizon - SEA_H;
  const float n = pal.night;
  // 1. the open sea up to the horizon: a crisp darker line, then lighter rows
  gfx_hspan(seaTop, 0, SCR_W, lerpCol(pal.waterDeep, pal.skyHorizon, 0.15f));
  for (int y = seaTop + 1; y < horizon; y++) {
    float k = (float)(y - seaTop) / SEA_H;
    gfx_hspan(y, 0, SCR_W, lerpCol(pal.water, pal.fog, 0.15f + 0.55f * k));
  }
  // 2. the sun's (moon's) glitter path below it
  if (sunX > -30 && sunX < SCR_W + 30) {
    const uint16_t g = n > 0.6f ? rgb(170, 180, 220) : pal.seaGlint;
    const int str = (int)(n > 0.6f ? 10 : 18 + 10 * (pal.sunY > 60 ? 1 : 0));
    const uint32_t tick = (uint32_t)(timeSec * 8.0f);
    for (int y = seaTop + 1; y < horizon; y++) {
      int spread = 4 + (y - seaTop) * 3;
      for (int k = 0; k < 4; k++) {
        uint32_t h = hash32((uint32_t)y * 97u + (uint32_t)k * 13u + tick * 7u);
        int dx = (int)(h % (uint32_t)(2 * spread + 1)) - spread;
        int len = 2 + (int)((h >> 8) & 3);
        gfx_hspanMode(y, sunX + dx - len / 2, sunX + dx + len / 2 + 1, g, GFX_ADD, str);
      }
    }
  }
  // 3. islands far out (hazy) and the headlands, sitting on the sea line
  const uint16_t farCol = lerpCol(pal.skylineFar, pal.skyHorizon, 0.40f);
  const uint16_t midLit = pal.skylineMid, midShade = darkenCol(pal.skylineMid, 0.82f);
  const bool sunRight = pal.sunDirX >= 0;
  const int farOff = (int)(skyOff * 0.3f), midOff = (int)(skyOff * 0.55f);
  for (int x = 0; x < SCR_W; x++) {
    const int fx = wrapBG(x + farOff), mx = wrapBG(x + midOff);
    const int fh = sFarH[fx], mh = sMidH[mx];
    if (mh) gfx_vspan(x, seaTop - mh, seaTop, slopeLit(mx, 2, sunRight) ? midLit : midShade);
    if (fh > mh) gfx_vspan(x, seaTop - fh, seaTop - mh, farCol);
  }
}

static void drawMountainLayers(float skyOff) {
  const int horizon = SCR_CY;
  const bool sunRight = pal.sunDirX >= 0;
  const uint16_t farLit = lerpCol(pal.skylineFar, pal.skyHorizon, 0.22f);
  const uint16_t farShade = lerpCol(darkenCol(pal.skylineFar, 0.80f), pal.skyHorizon, 0.22f);
  const uint16_t midLit = pal.skylineMid, midShade = darkenCol(pal.skylineMid, 0.78f);
  const uint16_t nearCol = pal.skylineNear;
  const uint16_t snowLit = pal.snowCap, snowShade = lerpCol(darkenCol(pal.snowCap, 0.78f), pal.skylineFar, 0.25f);
  const uint16_t mist = pal.fog;
  const int farOff = (int)(skyOff * 0.3f), midOff = (int)(skyOff * 0.55f), nearOff = (int)skyOff;
  for (int x = 0; x < SCR_W; x++) {
    const int fx = wrapBG(x + farOff), mx = wrapBG(x + midOff), nx = wrapBG(x + nearOff);
    const int fh = sFarH[fx], mh = sMidH[mx], nh = sNearH[nx];
    int bottom = horizon;
    if (nh) { gfx_vspan(x, horizon - nh, bottom, nearCol); bottom = horizon - nh; }
    if (mh > nh) {
      bool lit = slopeLit(mx, 2, sunRight);
      int top = horizon - mh;
      gfx_vspan(x, top, bottom, lit ? midLit : midShade);
      int sn = sSnow[mx] >> 4;
      if (sn) gfx_vspan(x, top, imin(top + sn, bottom), lit ? snowLit : snowShade);
      bottom = top;
    }
    if (fh > imax(mh, nh)) {
      bool lit = slopeLit(fx, 1, sunRight);
      int top = horizon - fh;
      gfx_vspan(x, top, bottom, lit ? farLit : farShade);
      int sn = sSnow[fx] & 15;
      if (sn) gfx_vspan(x, top, imin(top + sn, bottom), lit ? snowLit : snowShade);
    }
  }
  // mist lying in the valleys, thickening towards the ridge line
  for (int i = 0; i < 12; i++) {
    int y = horizon - 24 + i;
    gfx_hspanMode(y, 0, SCR_W, mist, GFX_BLEND, 1 + i / 2);
  }
}

// ═══════════════════════════════════════════════════════════════
//  SKY
// ═══════════════════════════════════════════════════════════════
void drawSky(float skyOff, float timeSec) {
  const int horizon = SCR_CY;
  bgTime = timeSec;

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
  int sunX = -1000;
  {
    int sx = wrapBG(SCR_W - (int)(skyOff * 0.25f) - 40) - BG_W / 4;
    int sy = (int)pal.sunY;
    if (sx > -80 && sx < SCR_W + 80) {
      sunX = sx;
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

  // 4. Clouds (behind every background layer)
  drawClouds(skyOff, timeSec);

  // 5. The theme's background layers
  if (sTheme == 1) drawCoastLayers(skyOff, timeSec, sunX);
  else if (sTheme == 2) drawMountainLayers(skyOff);
  else drawCityLayers(skyOff, timeSec);

  // 6. Horizon haze over the base of the layers
  for (int i = 0; i < 10; i++) {
    int y = horizon - 10 + i;
    gfx_hspanMode(y, 0, SCR_W, pal.fog, GFX_BLEND, 2 + i * 2);
  }
}
