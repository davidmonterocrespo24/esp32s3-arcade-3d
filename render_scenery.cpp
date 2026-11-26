/*
  ═══════════════════════════════════════════════════════════════
  SCENERY SPRITES — implementation
  Sizes are in world units (vertical px = scale * H * SCR_CY,
  horizontal px = scale * W * SCR_CX), the same projection as the road,
  lamps and buildings. Every colour is fogged towards pal.fog; man-made
  materials come lit from pal.prop[] (sun / moon + ambient), plants from
  the palette's foliage colours. Each sprite has a cheap LOD when small
  (one or two shapes) and is skipped below ~2 px.

  Props that run ALONG the road (fences, bus shelters, chalets) are drawn
  as boxes: their far end is the near end scaled towards the vanishing
  point by camDepth / (camDepth + depth * scale), so they line up with
  the road instead of facing the camera.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_scenery.h"
#include "render_road.h"
#include "rendering.h"
#include "gfx.h"
#include "colors.h"
#include "utils.h"
#include <math.h>

static inline uint16_t fogF(uint16_t c, float f) {
  return blend565(pal.fog, c, (uint32_t)(f * 32.0f + 0.5f));
}
static inline uint16_t propC(int pc, float fog) { return fogF(pal.prop[pc], fog); }

static inline void rectF(float x0, float y0, float x1, float y1, uint16_t c) {
  gfx_fillQuad(x0, y0, x1, y0, x1, y1, x0, y1, c);
}
// Vertical pole centred on x from y0 (bottom) up to y1, at least 1 px wide
static inline void pole(float x, float y0, float y1, float w, uint16_t c) {
  w = fmaxF(1.0f, w);
  rectF(x - w * 0.5f, y1, x + w * 0.5f, y0, c);
}

static void groundShadow(float sx, float sy, float w, float fog) {
  float day = 1.0f - pal.night;
  if (day <= 0.1f || w < 2) return;
  gfx_fillEllipse((int)sx, (int)sy, (int)(w * 0.6f), (int)fmaxF(1, w * 0.12f), COL_BLACK, GFX_BLEND,
                  (int)(8 * day * (1 - fog)));
}

// Far end of a prop that extends `depth` world units along the road:
// ratio towards the vanishing point (SCR_CX, SCR_CY).
static inline float depthRatio(float scale, float depth) {
  float cd = roadCam.camDepth > 0.01f ? roadCam.camDepth : 0.84f;
  return cd / (cd + depth * scale);
}
static inline float toVPx(float x, float r) { return SCR_CX + (x - SCR_CX) * r; }
static inline float toVPy(float y, float r) { return SCR_CY + (y - SCR_CY) * r; }

// ═══════════════════════════════════════════════════════════════
//  PLANTS
// ═══════════════════════════════════════════════════════════════
static void drawPine(float sx, float sy, float scale, float fog, bool snow) {
  float h = scale * 3200.0f * SCR_CY, w = scale * 1300.0f * SCR_CX;
  if (h < 3) return;
  uint16_t dk = fogF(pal.foliageD, fog), lt = fogF(pal.foliageL, fog);
  uint16_t sn = fogF(lerpCol(pal.ground[MAT_SNOW][0], pal.foliageL, 0.15f), fog);
  if (h < 8) {                                   // far: one triangle
    gfx_fillTri(sx, sy - h, sx - w * 0.5f, sy, sx + w * 0.5f, sy, snow ? lt : dk);
    return;
  }
  if (h < 34) {                                  // mid distance: one cone, shaded halves
    float base = sy - h * 0.10f;
    gfx_fillTri(sx, sy - h, sx - w * 0.5f, base, sx, base, dk);
    gfx_fillTri(sx, sy - h, sx, base, sx + w * 0.5f, base, lt);
    if (snow) gfx_fillTri(sx, sy - h, sx - w * 0.2f, sy - h * 0.62f, sx + w * 0.2f, sy - h * 0.62f, sn);
    return;
  }
  groundShadow(sx, sy, w, fog);
  float tw = fmaxF(1.0f, w * 0.12f), th = h * 0.16f;
  rectF(sx - tw * 0.5f, sy - th, sx + tw * 0.5f, sy, fogF(pal.trunk, fog));
  for (int i = 0; i < 3; i++) {   // three tiers, shaded halves
    float base = sy - th - h * 0.25f * i, top = base - h * 0.42f, half = w * (0.5f - 0.12f * i);
    gfx_fillTri(sx, top, sx - half, base, sx, base, dk);
    gfx_fillTri(sx, top, sx, base, sx + half, base, lt);
    if (snow && h > 10) {   // snow on the upper part of each tier
      float sb = top + (base - top) * 0.45f, sh = half * 0.45f;
      gfx_fillTri(sx, top, sx - sh, sb, sx + sh, sb, sn);
    }
  }
}

static void drawTree(float sx, float sy, float scale, float fog, float H, float W, bool street) {
  float h = scale * H * SCR_CY, w = scale * W * SCR_CX;
  if (h < 3) return;
  uint16_t dk = fogF(pal.foliageD, fog), lt = fogF(street ? lerpCol(pal.foliageL, COL_YELLOW, 0.08f) : pal.foliageL, fog);
  float r = w * 0.5f;
  if (h < 26) {                                  // far / mid: trunk + one crown
    if (h >= 8) pole(sx, sy, sy - h * 0.5f, w * 0.08f, fogF(pal.trunk, fog));
    gfx_fillEllipse((int)sx, (int)(sy - h + r), (int)fmaxF(1, r), (int)fmaxF(1, r * 0.9f), dk);
    return;
  }
  groundShadow(sx, sy, w, fog);
  float tw = fmaxF(1.0f, w * (street ? 0.07f : 0.10f)), th = h * (street ? 0.50f : 0.45f);
  gfx_fillQuad(sx - tw * 0.5f, sy, sx + tw * 0.5f, sy, sx + tw * 0.4f, sy - th, sx - tw * 0.4f, sy - th,
               fogF(pal.trunk, fog));
  if (street) {        // grate / tree guard at the base
    float gw = w * 0.22f, gh = fmaxF(1.0f, h * 0.05f);
    rectF(sx - gw, sy - gh, sx + gw, sy, propC(PC_DARKMETAL, fog));
  }
  int cyc = (int)(sy - th - r * 0.55f);
  gfx_fillEllipse((int)sx, cyc, (int)r, (int)(r * 0.9f), dk);
  gfx_fillEllipse((int)(sx + r * 0.2f), (int)(cyc - r * 0.2f), (int)(r * 0.7f), (int)(r * 0.62f), lt);
  if (!street) gfx_fillEllipse((int)(sx - r * 0.35f), (int)(cyc + r * 0.15f), (int)(r * 0.45f), (int)(r * 0.4f), dk);
}

static void drawPalm(float sx, float sy, float scale, float fog) {
  float h = scale * 3600.0f * SCR_CY, w = scale * 1800.0f * SCR_CX;
  if (h < 3) return;
  uint16_t trunk = fogF(lerpCol(pal.trunk, pal.rock, 0.35f), fog);
  uint16_t dk = fogF(pal.foliageD, fog), lt = fogF(pal.foliageL, fog);
  if (h < 8) {
    pole(sx, sy, sy - h, 1, trunk);
    gfx_fillEllipse((int)(sx + w * 0.16f), (int)(sy - h), (int)fmaxF(1, w * 0.3f), 1, dk);
    return;
  }
  if (h < 30) {                                  // mid distance: straight trunk, four fronds
    float tx = sx + w * 0.16f, ty = sy - h, r = w * 0.5f;
    gfx_fillQuad(sx - 0.5f, sy, sx + 0.5f, sy, tx + 0.5f, ty, tx - 0.5f, ty, trunk);
    float th = fmaxF(1.0f, r * 0.16f);
    gfx_fillTri(tx, ty - th, tx - r, ty + r * 0.35f, tx, ty + th, dk);
    gfx_fillTri(tx, ty - th, tx + r, ty + r * 0.35f, tx, ty + th, lt);
    gfx_fillTri(tx, ty, tx - r * 0.5f, ty - r * 0.3f, tx + r * 0.5f, ty - r * 0.3f, lt);
    return;
  }
  groundShadow(sx, sy, w * 0.6f, fog);
  // Slightly curved trunk in three pieces
  float tw = fmaxF(1.0f, w * 0.07f);
  float px = sx, py = sy;
  for (int i = 1; i <= 3; i++) {
    float t = i / 3.0f;
    float nx = sx + w * 0.16f * t * t, ny = sy - h * t;
    float wa = tw * (1.0f - 0.15f * (i - 1)), wb = tw * (1.0f - 0.15f * i);
    gfx_fillQuad(px - wa * 0.5f, py, px + wa * 0.5f, py, nx + wb * 0.5f, ny, nx - wb * 0.5f, ny, trunk);
    px = nx; py = ny;
  }
  // Crown: drooping fronds
  float r = w * 0.5f;
  static const float DIR[6][2] = { { -1.0f, 0.35f }, { -0.6f, -0.2f }, { 0.0f, -0.35f },
                                   { 0.6f, -0.2f }, { 1.0f, 0.35f }, { 0.25f, 0.55f } };
  for (int i = 0; i < 6; i++) {
    float ex = px + DIR[i][0] * r, ey = py + DIR[i][1] * r;
    float mx = (px + ex) * 0.5f, my = (py + ey) * 0.5f - r * 0.12f;
    float th = fmaxF(1.0f, r * 0.14f);
    gfx_fillTri(px, py - th, mx, my - th, ex, ey, (i & 1) ? lt : dk);
    gfx_fillTri(px, py + th * 0.5f, mx, my, ex, ey, dk);
  }
  if (h > 30) gfx_fillCircle((int)px, (int)(py + r * 0.06f), (int)fmaxF(1, r * 0.08f), fogF(pal.trunk, fog));
}

static void drawCypress(float sx, float sy, float scale, float fog) {
  float h = scale * 3400.0f * SCR_CY, w = scale * 640.0f * SCR_CX;
  if (h < 3) return;
  uint16_t dk = fogF(darkenCol(pal.foliageD, 0.78f), fog), lt = fogF(lerpCol(pal.foliageD, pal.foliageL, 0.35f), fog);
  if (h < 10) { gfx_fillTri(sx, sy - h, sx - w * 0.5f, sy, sx + w * 0.5f, sy, dk); return; }
  groundShadow(sx, sy, w * 1.3f, fog);
  float yb = sy - h * 0.03f;
  float L[10] = { sx, yb, sx - w * 0.30f, yb - h * 0.04f, sx - w * 0.5f, sy - h * 0.34f,
                  sx - w * 0.34f, sy - h * 0.72f, sx, sy - h };
  gfx_fillPoly(L, 5, dk);
  float R[10] = { sx, yb, sx, sy - h, sx + w * 0.32f, sy - h * 0.70f, sx + w * 0.5f, sy - h * 0.32f,
                  sx + w * 0.28f, yb - h * 0.04f };
  gfx_fillPoly(R, 5, lt);
  pole(sx, sy, yb, w * 0.12f, fogF(pal.trunk, fog));
}

static void drawBush(float sx, float sy, float scale, float fog) {
  float r = scale * 520.0f * SCR_CX;
  if (r < 1.5f) return;
  gfx_fillEllipse((int)sx, (int)(sy - r * 0.55f), (int)r, (int)fmaxF(1, r * 0.6f), fogF(pal.foliageD, fog));
  if (r < 3) return;
  gfx_fillEllipse((int)(sx + r * 0.25f), (int)(sy - r * 0.75f), (int)(r * 0.6f), (int)fmaxF(1, r * 0.4f),
                  fogF(pal.foliageL, fog));
}

static void drawFlowers(float sx, float sy, float scale, float fog) {
  float w = scale * 760.0f * SCR_CX, h = scale * 200.0f * SCR_CY;
  if (w < 2) return;
  gfx_fillEllipse((int)sx, (int)(sy - h * 0.35f), (int)(w * 0.5f), (int)fmaxF(1, h * 0.45f),
                  fogF(lerpCol(pal.foliageD, pal.grassD, 0.4f), fog));
  static const uint8_t COLS[4] = { PC_RED, PC_YELLOW, PC_WHITE, PC_PURPLE };
  if (w < 14) {
    gfx_fillRect((int)(sx - 1), (int)(sy - h * 0.6f), 2, 1, propC(PC_RED, fog));
    return;
  }
  float d = fmaxF(1.0f, w * 0.07f);
  for (int k = 0; k < 9; k++) {
    uint32_t hh = hash32((uint32_t)k * 2654435761u + 17u);
    float fx = ((hh & 255) / 255.0f - 0.5f) * 0.84f, fy = 0.25f + ((hh >> 8) & 255) / 255.0f * 0.7f;
    rectF(sx + fx * w - d * 0.5f, sy - fy * h * 1.6f - d * 0.5f, sx + fx * w + d * 0.5f, sy - fy * h * 1.6f + d * 0.5f,
          propC(COLS[k & 3], fog));
  }
}

static void drawGrassTuft(float sx, float sy, float scale, float fog) {
  float w = scale * 520.0f * SCR_CX, h = scale * 330.0f * SCR_CY;
  if (h < 2) return;
  uint16_t a = fogF(lerpCol(pal.foliageL, pal.grassL, 0.5f), fog), b = fogF(pal.foliageD, fog);
  if (h < 9) { gfx_fillTri(sx, sy - h, sx - w * 0.3f, sy, sx + w * 0.3f, sy, b); return; }
  static const float BL[5][2] = { { -0.42f, 0.7f }, { -0.18f, 1.0f }, { 0.04f, 0.85f }, { 0.26f, 0.95f }, { 0.46f, 0.6f } };
  float bw = fmaxF(1.0f, w * 0.08f);
  for (int k = 0; k < 5; k++) {
    float bx = sx + (k - 2) * w * 0.12f;
    gfx_fillTri(bx - bw, sy, bx + bw, sy, sx + BL[k][0] * w, sy - BL[k][1] * h, (k & 1) ? a : b);
  }
}

// ═══════════════════════════════════════════════════════════════
//  ROCKS
// ═══════════════════════════════════════════════════════════════
static void drawRock(float sx, float sy, float scale, float fog, float H, float W) {
  float h = scale * H * SCR_CY, w = scale * W * SCR_CX;
  if (h < 1.5f) return;
  uint16_t rc = fogF(pal.rock, fog);
  float v[12] = { sx - w * 0.5f, sy, sx - w * 0.4f, sy - h * 0.6f, sx - w * 0.1f, sy - h,
                  sx + w * 0.3f, sy - h * 0.85f, sx + w * 0.5f, sy - h * 0.3f, sx + w * 0.45f, sy };
  gfx_fillPoly(v, 6, darkenCol(rc, 0.7f));
  if (h < 4) return;
  float u[8] = { sx - w * 0.38f, sy - h * 0.6f, sx - w * 0.1f, sy - h * 0.98f,
                 sx + w * 0.28f, sy - h * 0.83f, sx + w * 0.05f, sy - h * 0.5f };
  gfx_fillPoly(u, 4, rc);
  if (h < 10) return;
  float t[8] = { sx + w * 0.05f, sy - h * 0.5f, sx + w * 0.28f, sy - h * 0.83f,
                 sx + w * 0.48f, sy - h * 0.3f, sx + w * 0.30f, sy - h * 0.12f };
  gfx_fillPoly(t, 4, darkenCol(rc, 0.85f));
}

// ═══════════════════════════════════════════════════════════════
//  ROAD FURNITURE
// ═══════════════════════════════════════════════════════════════
static void drawPost(float sx, float sy, float scale, float fog) {
  float h = scale * 330.0f * SCR_CY, w = fmaxF(0.5f, scale * 45.0f * SCR_CX);
  if (h < 2) return;
  rectF(sx - w, sy - h, sx + w, sy, propC(PC_WHITE, fog));
  float rh = fmaxF(1.0f, h * 0.16f);
  rectF(sx - w, sy - h * 0.82f - rh, sx + w, sy - h * 0.82f, fogF(COL_RED, fog));
  if (pal.night > 0.3f && h > 5)   // the reflector catches the headlights
    gfx_glow((int)sx, (int)(sy - h * 0.82f), (int)fmaxF(2, rh * 1.5f), rgb(255, 60, 40),
             (int)(22 * pal.night * (1 - fog)));
}

static void drawSnowPole(float sx, float sy, float scale, float fog) {
  float h = scale * 1150.0f * SCR_CY, w = fmaxF(1.0f, scale * 60.0f * SCR_CX);
  if (h < 2) return;
  uint16_t a = propC(PC_ORANGE, fog), b = propC(PC_BLACK, fog);
  if (h < 14) { pole(sx, sy, sy - h, w, a); return; }
  for (int k = 0; k < 5; k++)
    rectF(sx - w * 0.5f, sy - h * (k + 1) / 5.0f, sx + w * 0.5f, sy - h * k / 5.0f, (k & 1) ? b : a);
  if (pal.night > 0.3f)
    gfx_glow((int)sx, (int)(sy - h * 0.92f), (int)fmaxF(2, w * 2.5f), rgb(255, 150, 40),
             (int)(18 * pal.night * (1 - fog)));
}

static void drawChevron(float sx, float sy, float scale, float fog, bool right) {
  float h0 = scale * 420.0f * SCR_CY, h1 = scale * 1080.0f * SCR_CY, w = scale * 1000.0f * SCR_CX;
  if (h1 < 3) return;
  uint16_t board = propC(PC_BLACK, fog), arrow = propC(PC_YELLOW, fog);
  float yt = sy - h1, yb = sy - h0;
  if (w < 6) { rectF(sx - w * 0.5f, yt, sx + w * 0.5f, yb, arrow); return; }
  rectF(sx - w * 0.5f, yt, sx + w * 0.5f, yb, board);
  float ym = (yt + yb) * 0.5f, pad = (yb - yt) * 0.16f;
  float a = w * 0.10f, t = fmaxF(1.0f, w * 0.07f), d = right ? 1.0f : -1.0f;
  if (w < 18) {                                  // mid distance: three solid arrow heads
    for (int k = -1; k <= 1; k++) {
      float cx = sx + k * w * 0.30f;
      gfx_fillTri(cx - d * a, yt + pad, cx + d * a, ym, cx - d * a, yb - pad, arrow);
    }
    return;
  }
  uint16_t post = propC(PC_METAL, fog);
  pole(sx - w * 0.3f, sy, yb, scale * 50.0f * SCR_CX, post);
  pole(sx + w * 0.3f, sy, yb, scale * 50.0f * SCR_CX, post);
  for (int k = -1; k <= 1; k++) {
    float cx = sx + k * w * 0.30f;
    float x0 = cx - d * a, x1 = cx + d * a;    // arm start (back) and tip
    gfx_fillQuad(x0, yt + pad, x0 + d * t, yt + pad, x1 + d * t, ym, x1, ym, arrow);
    gfx_fillQuad(x1, ym, x1 + d * t, ym, x0 + d * t, yb - pad, x0, yb - pad, arrow);
  }
}

static void drawDistanceBoard(float sx, float sy, float scale, float fog, int stripes) {
  float h0 = scale * 320.0f * SCR_CY, h1 = scale * 1500.0f * SCR_CY, w = scale * 440.0f * SCR_CX;
  if (h1 < 3) return;
  uint16_t white = propC(PC_WHITE, fog), black = propC(PC_BLUE, fog);
  float yt = sy - h1, yb = sy - h0;
  pole(sx, sy, yb, scale * 70.0f * SCR_CX, propC(PC_METAL, fog));
  rectF(sx - w * 0.5f, yt, sx + w * 0.5f, yb, white);
  if (w < 4) return;
  // diagonal bars, evenly spaced down the board (they stay inside it)
  float bh = (yb - yt) / (2 * stripes + 1);
  float sk = fminF(w * 0.35f, bh * 0.8f);
  for (int k = 0; k < stripes; k++) {
    float y0 = yt + bh * (2 * k + 1) - sk * 0.5f;
    gfx_fillQuad(sx - w * 0.5f, y0 + sk, sx + w * 0.5f, y0, sx + w * 0.5f, y0 + bh, sx - w * 0.5f, y0 + bh + sk,
                 black);
  }
}

static void drawTrafficLight(float sx, float sy, float scale, float fog) {
  float h = scale * 2300.0f * SCR_CY, w = scale * 280.0f * SCR_CX;
  if (h < 3) return;
  uint16_t dark = propC(PC_DARKMETAL, fog);
  pole(sx, sy, sy - h * 0.72f, scale * 70.0f * SCR_CX, dark);
  if (h < 10) { rectF(sx - 1, sy - h, sx + 1, sy - h * 0.72f, dark); return; }
  float hb = sy - h * 0.70f, ht = sy - h;
  rectF(sx - w * 0.5f, ht, sx + w * 0.5f, hb, propC(PC_BLACK, fog));
  // phase: long green, short amber, red
  float ph = fmodf(bgTime, 14.0f);
  int on = ph < 9.0f ? 2 : (ph < 10.5f ? 1 : 0);
  static const uint16_t LIT[3] = { 0xF8A3, 0xFD20, 0x3FE8 };     // red, amber, green
  static const uint16_t OFF[3] = { 0x4000, 0x4200, 0x0200 };
  float lr = fmaxF(1.0f, w * 0.30f), step = (hb - ht) / 3.0f;
  for (int k = 0; k < 3; k++) {
    float cy = ht + step * (k + 0.5f);
    uint16_t c = k == on ? LIT[k] : OFF[k];
    gfx_fillEllipse((int)sx, (int)cy, (int)lr, (int)lr, fogF(c, fog * 0.6f));
    if (k == on && h > 24) gfx_glow((int)sx, (int)cy, (int)fmaxF(3, lr * (2.5f + 2.0f * pal.night)), LIT[k],
                                    (int)((6 + 14 * pal.night) * (1 - fog)));
  }
}

static void drawFlag(float sx, float sy, float scale, float fog) {
  float h = scale * 3400.0f * SCR_CY, fw = scale * 820.0f * SCR_CX, fh = scale * 540.0f * SCR_CY;
  if (h < 3) return;
  pole(sx, sy, sy - h, scale * 60.0f * SCR_CX, propC(PC_WHITE, fog));
  if (fw < 2) return;
  float top = sy - h, wave = sinf(bgTime * 5.0f + sx * 0.05f) * fh * 0.12f;
  const int C = fw >= 12 ? 4 : 1, R = fw >= 12 ? 3 : 1;
  for (int i = 0; i < C; i++) {
    float x0 = sx + fw * i / C, x1 = sx + fw * (i + 1) / C;
    float w0 = wave * i / C, w1 = wave * (i + 1) / C;
    for (int j = 0; j < R; j++) {
      float y0 = top + fh * j / R, y1 = top + fh * (j + 1) / R;
      uint16_t c = (C == 1) ? propC(PC_METAL, fog) : propC(((i + j) & 1) ? PC_BLACK : PC_WHITE, fog);
      gfx_fillQuad(x0, y0 + w0, x1, y0 + w1, x1, y1 + w1, x0, y1 + w0, c);
    }
  }
}

// Fence running along the road from this boundary (FENCE_LEN long)
static void drawFence(float sx, float sy, float scale, float fog) {
  float h = scale * 430.0f * SCR_CY;
  if (h < 1.5f) return;
  float r = depthRatio(scale, FENCE_LEN);
  float fx = toVPx(sx, r), fy = toVPy(sy, r), fh = h * r;
  uint16_t wood = propC(PC_WOOD, fog), dark = propC(PC_DARKWOOD, fog);
  for (int k = 0; k < 2; k++) {          // two rails
    float a = k ? 0.42f : 0.84f, t = fmaxF(0.8f, h * 0.10f);
    gfx_fillQuad(sx, sy - h * a, fx, fy - fh * a, fx, fy - fh * a + fmaxF(0.8f, t * r), sx, sy - h * a + t, wood);
  }
  float pw = fmaxF(1.0f, scale * 70.0f * SCR_CX);
  pole(sx, sy, sy - h, pw, dark);
  float mx = toVPx(sx, (1 + r) * 0.5f), my = toVPy(sy, (1 + r) * 0.5f);
  pole(mx, my, my - h * (1 + r) * 0.5f, pw * (1 + r) * 0.5f, dark);
}

// ═══════════════════════════════════════════════════════════════
//  CITY PROPS
// ═══════════════════════════════════════════════════════════════
static void drawPlanter(float sx, float sy, float scale, float fog) {
  float w = scale * 720.0f * SCR_CX, h = scale * 330.0f * SCR_CY;
  if (h < 1.5f) return;
  uint16_t c = propC(PC_CONCRETE, fog);
  rectF(sx - w * 0.5f, sy - h, sx + w * 0.5f, sy, c);
  if (h < 3) return;
  rectF(sx - w * 0.5f, sy - h, sx + w * 0.5f, sy - h * 0.82f, darkenCol(c, 0.85f));
  float r = w * 0.46f;
  gfx_fillEllipse((int)sx, (int)(sy - h - r * 0.35f), (int)r, (int)fmaxF(1, r * 0.55f), fogF(pal.foliageD, fog));
  gfx_fillEllipse((int)(sx + r * 0.2f), (int)(sy - h - r * 0.55f), (int)(r * 0.55f), (int)fmaxF(1, r * 0.35f),
                  fogF(pal.foliageL, fog));
}

// Bus shelter: a box along the road (glass side towards the road)
static void drawBusStop(float sx, float sy, float scale, float fog) {
  float h = scale * 1050.0f * SCR_CY, w = scale * 380.0f * SCR_CX;
  if (h < 3) return;
  const float depth = 1500.0f;
  float r = depthRatio(scale, depth);
  bool leftOfCentre = sx < SCR_CX;
  float ex = leftOfCentre ? sx + w * 0.5f : sx - w * 0.5f;     // edge towards the road
  float fx = toVPx(ex, r), fy = toVPy(sy, r), fh = h * r;
  uint16_t frame = propC(PC_DARKMETAL, fog), glass = propC(PC_GLASS, fog), roof = propC(PC_METAL, fog);
  // glass side (blended), frame posts, roof slab
  gfx_fillQuad(ex, sy - h * 0.1f, fx, fy - fh * 0.1f, fx, fy - fh * 0.92f, ex, sy - h * 0.92f, glass, GFX_BLEND, 18);
  pole(ex, sy, sy - h, scale * 60.0f * SCR_CX, frame);
  pole(fx, fy, fy - fh, scale * r * 60.0f * SCR_CX, frame);
  gfx_fillQuad(ex, sy - h, fx, fy - fh, fx, fy - fh * 0.92f, ex, sy - h * 0.92f, roof);
  // the near end: an advertising panel, lit at night
  uint16_t ad = lerpCol(propC(PC_BLUE, fog), lerpCol(pal.lampLight, COL_WHITE, 0.5f), pal.night * 0.8f);
  rectF(sx - w * 0.5f, sy - h * 0.92f, sx + w * 0.5f, sy - h * 0.12f, frame);
  rectF(sx - w * 0.38f, sy - h * 0.84f, sx + w * 0.38f, sy - h * 0.2f, ad);
  if (h > 14 && pal.night > 0.3f)
    gfx_glow((int)sx, (int)(sy - h * 0.5f), (int)(w * 0.9f), pal.lampLight, (int)(10 * pal.night * (1 - fog)));
  // bus-stop sign
  float px = leftOfCentre ? sx - w * 0.8f : sx + w * 0.8f;
  pole(px, sy, sy - h * 1.25f, scale * 40.0f * SCR_CX, frame);
  if (h > 10) gfx_fillCircle((int)px, (int)(sy - h * 1.25f), (int)fmaxF(1, w * 0.18f), propC(PC_YELLOW, fog));
}

// Vertical neon blade sign fixed to a facade
static void drawNeon(float sx, float sy, float scale, float fog, bool pink) {
  float h0 = scale * 1300.0f * SCR_CY, h1 = scale * 3700.0f * SCR_CY, w = scale * 300.0f * SCR_CX;
  if (h1 < 4) return;
  const uint16_t tube = pink ? rgb(255, 60, 200) : rgb(40, 230, 255);
  float n = pal.night;
  uint16_t panel = fogF(rgb(26, 22, 34), fog);
  uint16_t lit = fogF(lerpCol(darkenCol(tube, 0.45f), tube, 0.35f + 0.65f * n), fog * (1.0f - 0.6f * n));
  float yt = sy - h1, yb = sy - h0;
  rectF(sx - w * 0.5f, yt, sx + w * 0.5f, yb, panel);
  float b = fmaxF(1.0f, w * 0.12f);
  if (w < 7) { rectF(sx - w * 0.25f, yt, sx + w * 0.25f, yb, lit); return; }
  rectF(sx - w * 0.5f, yt, sx + w * 0.5f, yt + b, lit);
  rectF(sx - w * 0.5f, yb - b, sx + w * 0.5f, yb, lit);
  rectF(sx - w * 0.5f, yt, sx - w * 0.5f + b, yb, lit);
  rectF(sx + w * 0.5f - b, yt, sx + w * 0.5f, yb, lit);
  // letters, one per row, when there is room for the 5x7 font
  const char* word = pink ? "BAR" : "HOTEL";
  int len = pink ? 3 : 5;
  float rowH = (yb - yt - 2 * b) / len;
  if (w >= 9 && rowH >= 9) {
    for (int k = 0; k < len; k++) {
      char s[2] = { word[k], 0 };
      gfx_text((int)(sx - 2.5f), (int)(yt + b + rowH * k + (rowH - 7) * 0.5f), s, lit, 1);
    }
  } else {
    rectF(sx - w * 0.12f, yt + b * 2, sx + w * 0.12f, yb - b * 2, lit);
  }
  if (n > 0.2f && h1 > 12) {
    int a = (int)(14 * n * (1 - fog));
    gfx_glow((int)sx, (int)((yt + yb) * 0.5f), (int)fminF(28.0f, fmaxF(4, (yb - yt) * 0.55f)), tube, a);
  }
}

static void drawBillboard(float sx, float sy, float scale, float fog, int design) {
  float w = scale * 3000.0f * SCR_CX, h0 = scale * 1300.0f * SCR_CY, h1 = scale * 2900.0f * SCR_CY;
  if (h1 < 3) return;
  static const uint8_t BG[3] = { PC_RED, PC_BLUE, PC_BLACK };
  uint16_t frame = propC(PC_DARKMETAL, fog);
  uint16_t bg = design == 2 ? fogF(rgb(18, 16, 30), fog) : propC(BG[design], fog);
  float yt = sy - h1, yb = sy - h0;
  float lw = scale * 90.0f * SCR_CX;
  pole(sx - w * 0.3f, sy, yb, lw, frame);
  pole(sx + w * 0.3f, sy, yb, lw, frame);
  float f = fmaxF(1.0f, (yb - yt) * 0.05f);
  rectF(sx - w * 0.5f - f, yt - f, sx + w * 0.5f + f, yb + f, frame);
  rectF(sx - w * 0.5f, yt, sx + w * 0.5f, yb, bg);
  if (w < 10) return;
  float bh = yb - yt;
  // artwork: a stripe and a word
  static const char* const WORDS[3] = { "TURBO", "ARCADE", "NEON" };
  uint16_t ink = design == 2 ? fogF(lerpCol(rgb(150, 40, 120), rgb(255, 70, 210), 0.3f + 0.7f * pal.night), fog * (1 - 0.5f * pal.night))
                             : propC(PC_WHITE, fog);
  uint16_t accent = design == 0 ? propC(PC_YELLOW, fog) : (design == 1 ? propC(PC_ORANGE, fog)
                                                                       : fogF(rgb(40, 220, 255), fog * (1 - 0.5f * pal.night)));
  rectF(sx - w * 0.5f, yb - bh * 0.22f, sx + w * 0.5f, yb - bh * 0.10f, accent);
  const char* word = WORDS[design];
  int sc = bh >= 44 ? 3 : (bh >= 26 ? 2 : (bh >= 12 ? 1 : 0));
  while (sc > 0 && gfx_textWidth(word, sc) > w * 0.9f) sc--;
  if (sc > 0) {
    int tw = gfx_textWidth(word, sc);
    gfx_text((int)(sx - tw * 0.5f), (int)(yt + bh * 0.42f - 3.5f * sc), word, ink, sc);
  } else {
    rectF(sx - w * 0.3f, yt + bh * 0.3f, sx + w * 0.3f, yt + bh * 0.5f, ink);
  }
  if (pal.night > 0.25f) {   // floodlit at night
    int a = (int)(7 * pal.night * (1 - fog));
    gfx_fillRectMode((int)(sx - w * 0.5f), (int)yt, (int)w, (int)bh, pal.lampLight, GFX_ADD, a);
    if (design == 2 && h1 > 16)
      gfx_glow((int)sx, (int)(yt + bh * 0.45f), (int)fmaxF(4, w * 0.45f), rgb(255, 60, 200), (int)(10 * pal.night * (1 - fog)));
  }
}

// ═══════════════════════════════════════════════════════════════
//  COAST PROPS
// ═══════════════════════════════════════════════════════════════
static void drawUmbrella(float sx, float sy, float scale, float fog, bool blue) {
  float h = scale * 950.0f * SCR_CY, w = scale * 1200.0f * SCR_CX;
  if (h < 2) return;
  uint16_t ca = propC(blue ? PC_BLUE : PC_RED, fog), cb = propC(blue ? PC_YELLOW : PC_WHITE, fog);
  float top = sy - h, rim = sy - h * 0.70f;
  if (h < 14) { pole(sx, sy, top, 1, propC(PC_WHITE, fog)); gfx_fillTri(sx, top, sx - w * 0.5f, rim, sx + w * 0.5f, rim, ca); return; }
  if (pal.night < 0.7f)   // shade on the sand
    gfx_fillEllipse((int)(sx + w * 0.08f), (int)sy, (int)(w * 0.45f), (int)fmaxF(1, h * 0.06f), COL_BLACK, GFX_BLEND,
                    (int)(9 * (1 - pal.night) * (1 - fog)));
  pole(sx, sy, top, scale * 45.0f * SCR_CX, propC(PC_WHITE, fog));
  const int K = 6;
  for (int k = 0; k < K; k++) {
    float f0 = -0.5f + (float)k / K, f1 = -0.5f + (float)(k + 1) / K;
    float d0 = rim + h * 0.05f * (1.0f - fabsf(f0) * 2.0f), d1 = rim + h * 0.05f * (1.0f - fabsf(f1) * 2.0f);
    gfx_fillTri(sx, top, sx + f0 * w, d0, sx + f1 * w, d1, (k & 1) ? cb : ca);
  }
}

static void drawSailboat(float sx, float sy, float scale, float fog) {
  float w = scale * 1500.0f * SCR_CX, h = scale * 2700.0f * SCR_CY;
  if (h < 3) return;
  float bob = sinf(bgTime * 1.6f + sx * 0.02f) * h * 0.012f;
  sy += bob;
  uint16_t hull = propC(PC_WHITE, fog), sail = propC(PC_WHITE, fog * 0.8f);
  float hh = h * 0.10f;
  gfx_fillQuad(sx - w * 0.5f, sy - hh, sx + w * 0.5f, sy - hh, sx + w * 0.36f, sy, sx - w * 0.40f, sy, hull);
  if (h > 10) rectF(sx - w * 0.5f, sy - hh, sx + w * 0.5f, sy - hh * 0.7f, propC(PC_BLUE, fog));
  pole(sx, sy - hh, sy - h, fmaxF(1, scale * 40 * SCR_CX), propC(PC_METAL, fog));
  gfx_fillTri(sx + 1, sy - h * 0.96f, sx + 1, sy - hh * 1.6f, sx + w * 0.42f, sy - hh * 1.6f, sail);
  gfx_fillTri(sx - 1, sy - h * 0.86f, sx - 1, sy - hh * 1.8f, sx - w * 0.36f, sy - hh * 1.8f,
              darkenCol(sail, 0.9f));
}

static void drawLighthouse(float sx, float sy, float scale, float fog) {
  float H = scale * 9500.0f * SCR_CY, wb = scale * 1300.0f * SCR_CX, wt = scale * 820.0f * SCR_CX;
  if (H < 4) return;
  uint16_t white = propC(PC_WHITE, fog), red = propC(PC_RED, fog), dark = propC(PC_DARKMETAL, fog);
  float body = H * 0.80f;
  auto widthAt = [&](float t) { return wb + (wt - wb) * t; };   // t = 0 base .. 1 top of the body
  if (H < 12) {
    gfx_fillQuad(sx - wb * 0.5f, sy, sx + wb * 0.5f, sy, sx + wt * 0.5f, sy - body, sx - wt * 0.5f, sy - body, white);
    rectF(sx - wt * 0.4f, sy - H, sx + wt * 0.4f, sy - body, red);
  } else {
    groundShadow(sx, sy, wb * 1.6f, fog);
    // keeper's house at the foot
    rectF(sx - wb * 1.1f, sy - H * 0.09f, sx + wb * 0.2f, sy, darkenCol(white, 0.92f));
    gfx_fillTri(sx - wb * 1.2f, sy - H * 0.09f, sx + wb * 0.3f, sy - H * 0.09f, sx - wb * 0.45f, sy - H * 0.14f, red);
    // tower: white with three red bands, shaded on the left
    static const float BANDS[4][2] = { { 0.0f, 0.18f }, { 0.36f, 0.50f }, { 0.68f, 0.82f }, { 0.0f, 0.0f } };
    float tcur = 0;
    for (int b = 0; b < 4; b++) {
      float t0 = BANDS[b][0], t1 = BANDS[b][1];
      if (b == 3) { t0 = 1; t1 = 1; }
      // white piece below this band
      if (t0 > tcur) {
        float w0 = widthAt(tcur), w1 = widthAt(t0);
        gfx_fillQuad(sx - w0 * 0.5f, sy - body * tcur, sx + w0 * 0.5f, sy - body * tcur,
                     sx + w1 * 0.5f, sy - body * t0, sx - w1 * 0.5f, sy - body * t0, white);
      }
      if (t1 > t0) {
        float w0 = widthAt(t0), w1 = widthAt(t1);
        gfx_fillQuad(sx - w0 * 0.5f, sy - body * t0, sx + w0 * 0.5f, sy - body * t0,
                     sx + w1 * 0.5f, sy - body * t1, sx - w1 * 0.5f, sy - body * t1, red);
      }
      tcur = t1;
    }
    // round shading: a darker strip on the left third
    gfx_fillQuad(sx - wb * 0.5f, sy, sx - wb * 0.18f, sy, sx - wt * 0.18f, sy - body, sx - wt * 0.5f, sy - body,
                 COL_BLACK, GFX_BLEND, 7);
    // gallery, lantern, dome
    rectF(sx - wt * 0.75f, sy - body - H * 0.03f, sx + wt * 0.75f, sy - body, dark);
    uint16_t glass = lerpCol(propC(PC_GLASS, fog), lerpCol(pal.lampLight, COL_WHITE, 0.5f), fminF(1.0f, pal.night * 1.6f));
    rectF(sx - wt * 0.42f, sy - H * 0.93f, sx + wt * 0.42f, sy - body - H * 0.03f, glass);
    gfx_fillTri(sx - wt * 0.5f, sy - H * 0.93f, sx + wt * 0.5f, sy - H * 0.93f, sx, sy - H, red);
  }
  // the light: a glow and a slowly sweeping beam from late afternoon on
  float lightK = clampF((pal.night - 0.1f) * 1.6f, 0, 1);
  if (lightK > 0 && H > 8) {
    int cx = (int)sx, cy = (int)(sy - H * 0.88f);
    float ang = bgTime * 1.3f;
    float c = cosf(ang), s = sinf(ang);
    float L = H * (0.9f + 0.7f * fabsf(s)), hw = H * 0.07f;
    int a = (int)(14 * lightK * (1 - fog));
    if (a > 0) {
      float ex = cx + s * L;
      gfx_fillTri((float)cx, (float)cy - 1, ex, cy - hw, ex, cy + hw, pal.lampLight, GFX_ADD, a);
      gfx_fillTri((float)cx, (float)cy, ex, cy - hw * 0.4f, ex, cy + hw * 0.4f, pal.lampLight, GFX_ADD, a);
    }
    int flash = c > 0.85f ? (int)((c - 0.85f) * 120.0f) : 0;   // the beam faces the camera
    gfx_glow(cx, cy, (int)fmaxF(3, wt * (1.5f + flash * 0.08f)), pal.lampLight, (int)((10 + flash) * lightK * (1 - fog)));
  }
}

// ═══════════════════════════════════════════════════════════════
//  MOUNTAIN PROPS
// ═══════════════════════════════════════════════════════════════
// Alpine hut: gable end towards the camera, the long side along the road
static void drawChalet(float sx, float sy, float scale, float fog) {
  float W = scale * 2400.0f * SCR_CX, Hw = scale * 1300.0f * SCR_CY, Hr = scale * 2500.0f * SCR_CY;
  if (Hr < 3) return;
  uint16_t wood = propC(PC_WOOD, fog), dwood = propC(PC_DARKWOOD, fog);
  uint16_t base = propC(PC_STONE, fog), roof = fogF(darkenCol(pal.prop[PC_DARKWOOD], 0.7f), fog);
  if (Hr < 10) {
    rectF(sx - W * 0.5f, sy - Hw, sx + W * 0.5f, sy, wood);
    gfx_fillTri(sx - W * 0.6f, sy - Hw, sx + W * 0.6f, sy - Hw, sx, sy - Hr, roof);
    return;
  }
  const float depth = 3000.0f;
  float r = depthRatio(scale, depth);
  bool leftOfCentre = sx < SCR_CX;
  float ex = leftOfCentre ? sx + W * 0.5f : sx - W * 0.5f;
  float fx = toVPx(ex, r), fy = toVPy(sy, r);
  // long side (towards the road), with its roof plane
  gfx_fillQuad(ex, sy, fx, fy, fx, fy - Hw * r, ex, sy - Hw, dwood);
  float rx = toVPx(sx, r), ry = toVPy(sy - Hr, r);
  gfx_fillQuad(ex + (leftOfCentre ? W * 0.1f : -W * 0.1f), sy - Hw * 0.95f, fx, fy - Hw * r * 0.95f, rx, ry, sx, sy - Hr, roof);
  // gable end
  rectF(sx - W * 0.5f, sy - Hw, sx + W * 0.5f, sy, wood);
  rectF(sx - W * 0.5f, sy - Hw * 0.22f, sx + W * 0.5f, sy, base);
  gfx_fillTri(sx - W * 0.5f, sy - Hw, sx + W * 0.5f, sy - Hw, sx, sy - Hr * 0.94f, wood);
  gfx_fillQuad(sx - W * 0.62f, sy - Hw * 0.92f, sx - W * 0.5f, sy - Hw * 0.92f, sx, sy - Hr, sx, sy - Hr * 1.04f, roof);
  gfx_fillQuad(sx + W * 0.5f, sy - Hw * 0.92f, sx + W * 0.62f, sy - Hw * 0.92f, sx, sy - Hr * 1.04f, sx, sy - Hr, roof);
  // balcony and windows (lit at night)
  rectF(sx - W * 0.5f, sy - Hw * 0.98f, sx + W * 0.5f, sy - Hw * 0.90f, dwood);
  uint16_t win = lerpCol(fogF(rgb(40, 44, 56), fog), pal.windowLit, pal.night);
  for (int k = -1; k <= 1; k += 2) {
    float wx = sx + k * W * 0.25f;
    rectF(wx - W * 0.09f, sy - Hw * 0.70f, wx + W * 0.09f, sy - Hw * 0.40f, win);
    rectF(wx - W * 0.16f, sy - Hw * 0.70f, wx - W * 0.09f, sy - Hw * 0.40f, propC(PC_RED, fog));
    rectF(wx + W * 0.09f, sy - Hw * 0.70f, wx + W * 0.16f, sy - Hw * 0.40f, propC(PC_RED, fog));
  }
  rectF(sx - W * 0.07f, sy - Hr * 0.62f, sx + W * 0.07f, sy - Hr * 0.50f, win);
}

// ═══════════════════════════════════════════════════════════════
//  DISPATCH
// ═══════════════════════════════════════════════════════════════
void drawSceneSprite(int type, float sx, float sy, float scale, float fog) {
  if (!(scale > 0)) return;
  float reach = scale * 3200.0f * SCR_CX;   // generous half-width for culling
  if (type == SPR_FENCE || type == SPR_BUS_STOP || type == SPR_CHALET) reach = SCR_W;   // boxes reach the centre
  if (sx + reach < 0 || sx - reach > SCR_W) return;
  if (sy < gfx_cy0) return;                 // base above the clip: hidden behind a crest
  switch (type) {
    case SPR_PINE:          drawPine(sx, sy, scale, fog, false); break;
    case SPR_PINE_SNOW:     drawPine(sx, sy, scale, fog, true); break;
    case SPR_TREE:          drawTree(sx, sy, scale, fog, 2600.0f, 1700.0f, false); break;
    case SPR_STREET_TREE:   drawTree(sx, sy, scale, fog, 1900.0f, 1000.0f, true); break;
    case SPR_PALM:          drawPalm(sx, sy, scale, fog); break;
    case SPR_CYPRESS:       drawCypress(sx, sy, scale, fog); break;
    case SPR_BUSH:          drawBush(sx, sy, scale, fog); break;
    case SPR_FLOWERS:       drawFlowers(sx, sy, scale, fog); break;
    case SPR_GRASS:         drawGrassTuft(sx, sy, scale, fog); break;
    case SPR_ROCK:          drawRock(sx, sy, scale, fog, 750.0f, 1100.0f); break;
    case SPR_BOULDER:       drawRock(sx, sy, scale, fog, 1250.0f, 1700.0f); break;
    case SPR_POST:          drawPost(sx, sy, scale, fog); break;
    case SPR_SNOW_POLE:     drawSnowPole(sx, sy, scale, fog); break;
    case SPR_FENCE:         drawFence(sx, sy, scale, fog); break;
    case SPR_CHEVRON_L:     drawChevron(sx, sy, scale, fog, false); break;
    case SPR_CHEVRON_R:     drawChevron(sx, sy, scale, fog, true); break;
    case SPR_BOARD_3:       drawDistanceBoard(sx, sy, scale, fog, 3); break;
    case SPR_BOARD_2:       drawDistanceBoard(sx, sy, scale, fog, 2); break;
    case SPR_BOARD_1:       drawDistanceBoard(sx, sy, scale, fog, 1); break;
    case SPR_BILLBOARD_A:   drawBillboard(sx, sy, scale, fog, 0); break;
    case SPR_BILLBOARD_B:   drawBillboard(sx, sy, scale, fog, 1); break;
    case SPR_BILLBOARD_C:   drawBillboard(sx, sy, scale, fog, 2); break;
    case SPR_LIGHTHOUSE:    drawLighthouse(sx, sy, scale, fog); break;
    case SPR_UMBRELLA:      drawUmbrella(sx, sy, scale, fog, false); break;
    case SPR_UMBRELLA_B:    drawUmbrella(sx, sy, scale, fog, true); break;
    case SPR_TRAFFIC_LIGHT: drawTrafficLight(sx, sy, scale, fog); break;
    case SPR_BUS_STOP:      drawBusStop(sx, sy, scale, fog); break;
    case SPR_PLANTER:       drawPlanter(sx, sy, scale, fog); break;
    case SPR_NEON_PINK:     drawNeon(sx, sy, scale, fog, true); break;
    case SPR_NEON_CYAN:     drawNeon(sx, sy, scale, fog, false); break;
    case SPR_SAILBOAT:      drawSailboat(sx, sy, scale, fog); break;
    case SPR_CHALET:        drawChalet(sx, sy, scale, fog); break;
    case SPR_FLAG:          drawFlag(sx, sy, scale, fog); break;
    default: break;
  }
}

// Footprint width (road half-widths): what must stay clear of the road,
// of a facade or of a rail. Crowns and boards may overhang it.
float spriteWidth(int type) {
  switch (type) {
    case SPR_PINE: case SPR_PINE_SNOW: case SPR_TREE: return 0.24f;
    case SPR_STREET_TREE: return 0.20f;
    case SPR_PALM: return 0.20f;
    case SPR_CYPRESS: return 0.30f;
    case SPR_BUSH: return 0.50f;
    case SPR_FLOWERS: return 0.38f;
    case SPR_GRASS: return 0.26f;
    case SPR_ROCK: return 0.55f;
    case SPR_BOULDER: return 0.85f;
    case SPR_POST: case SPR_SNOW_POLE: return 0.04f;
    case SPR_FENCE: return 0.10f;
    case SPR_CHEVRON_L: case SPR_CHEVRON_R: return 0.34f;    // the posts (the board overhangs)
    case SPR_BOARD_3: case SPR_BOARD_2: case SPR_BOARD_1: return 0.22f;
    case SPR_BILLBOARD_A: case SPR_BILLBOARD_B: case SPR_BILLBOARD_C: return 1.50f;
    case SPR_LIGHTHOUSE: return 0.70f;
    case SPR_UMBRELLA: case SPR_UMBRELLA_B: return 0.60f;
    case SPR_TRAFFIC_LIGHT: return 0.14f;
    case SPR_BUS_STOP: return 0.30f;
    case SPR_PLANTER: return 0.34f;
    case SPR_NEON_PINK: case SPR_NEON_CYAN: return 0.10f;
    case SPR_SAILBOAT: return 0.75f;
    case SPR_CHALET: return 1.25f;
    case SPR_FLAG: return 0.06f;
    default: return 0.25f;
  }
}

float spriteHitWidth(int type) {
  switch (type) {
    case SPR_PINE: case SPR_PINE_SNOW: case SPR_TREE: return 0.10f;
    case SPR_STREET_TREE: return 0.08f;
    case SPR_PALM: return 0.08f;
    case SPR_CYPRESS: return 0.12f;
    case SPR_ROCK: return 0.32f;
    case SPR_BOULDER: return 0.60f;
    case SPR_POST: return 0.05f;
    case SPR_SNOW_POLE: return 0.04f;
    case SPR_CHEVRON_L: case SPR_CHEVRON_R: return 0.34f;    // a board on two posts
    case SPR_BOARD_3: case SPR_BOARD_2: case SPR_BOARD_1: return 0.06f;
    case SPR_BILLBOARD_A: case SPR_BILLBOARD_B: case SPR_BILLBOARD_C: return 1.20f;
    case SPR_LIGHTHOUSE: return 0.60f;
    case SPR_TRAFFIC_LIGHT: return 0.05f;
    case SPR_BUS_STOP: return 0.30f;
    case SPR_PLANTER: return 0.34f;
    case SPR_CHALET: return 1.20f;
    case SPR_FLAG: return 0.04f;
    default: return 0.0f;   // bushes, flowers, grass, fences, umbrellas, neon signs, boats: soft
  }
}
