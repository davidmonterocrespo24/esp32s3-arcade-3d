/*
  ═══════════════════════════════════════════════════════════════
  SCENERY SPRITES — implementation
  Sizes are in world units (vertical px = scale * H * SCR_CY,
  horizontal px = scale * W * SCR_CX), the same projection as the
  road, lamps and buildings. (The first version used raw pixel
  factors that made trees about as tall as the car.)
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_scenery.h"
#include "gfx.h"
#include "colors.h"
#include "utils.h"
#include <math.h>

static inline uint16_t fogF(uint16_t c, float f) {
  return blend565(pal.fog, c, (uint32_t)(f * 32.0f + 0.5f));
}

static void groundShadow(float sx, float sy, float w, float fog) {
  float day = 1.0f - pal.night;
  if (day <= 0.1f || w < 2) return;
  gfx_fillEllipse((int)sx, (int)sy, (int)(w * 0.6f), (int)fmaxF(1, w * 0.12f), COL_BLACK, GFX_BLEND,
                  (int)(8 * day * (1 - fog)));
}

static void drawPine(float sx, float sy, float scale, float fog, bool snow) {
  float h = scale * 3200.0f * SCR_CY, w = scale * 1300.0f * SCR_CX;
  if (h < 3) return;
  groundShadow(sx, sy, w, fog);
  float tw = fmaxF(1.0f, w * 0.12f), th = h * 0.16f;
  gfx_fillQuad(sx - tw * 0.5f, sy, sx + tw * 0.5f, sy, sx + tw * 0.5f, sy - th, sx - tw * 0.5f, sy - th,
               fogF(pal.trunk, fog));
  uint16_t dk = fogF(pal.foliageD, fog), lt = fogF(pal.foliageL, fog);
  uint16_t sn = fogF(lerpCol(pal.ground[MAT_SNOW][0], pal.foliageL, 0.15f), fog);
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

static void drawTree(float sx, float sy, float scale, float fog) {
  float h = scale * 2600.0f * SCR_CY, w = scale * 1700.0f * SCR_CX;
  if (h < 3) return;
  groundShadow(sx, sy, w, fog);
  float tw = fmaxF(1.0f, w * 0.10f), th = h * 0.45f;
  gfx_fillQuad(sx - tw * 0.5f, sy, sx + tw * 0.5f, sy, sx + tw * 0.4f, sy - th, sx - tw * 0.4f, sy - th,
               fogF(pal.trunk, fog));
  float r = w * 0.5f;
  int cyc = (int)(sy - th - r * 0.55f);
  gfx_fillEllipse((int)sx, cyc, (int)r, (int)(r * 0.9f), fogF(pal.foliageD, fog));
  gfx_fillEllipse((int)(sx + r * 0.2f), (int)(cyc - r * 0.2f), (int)(r * 0.7f), (int)(r * 0.62f),
                  fogF(pal.foliageL, fog));
}

static void drawPalm(float sx, float sy, float scale, float fog) {
  float h = scale * 3600.0f * SCR_CY, w = scale * 1800.0f * SCR_CX;
  if (h < 3) return;
  groundShadow(sx, sy, w * 0.6f, fog);
  // Slightly curved trunk in three pieces
  uint16_t trunk = fogF(lerpCol(pal.trunk, pal.rock, 0.35f), fog);
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
  uint16_t dk = fogF(pal.foliageD, fog), lt = fogF(pal.foliageL, fog);
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
}

void drawSceneSprite(int type, float sx, float sy, float scale, float fog) {
  if (!(scale > 0)) return;
  float reach = scale * 2000.0f * SCR_CX;   // generous half-width for culling
  if (sx + reach < 0 || sx - reach > SCR_W) return;
  switch (type) {
    case SPR_PINE:      drawPine(sx, sy, scale, fog, false); break;
    case SPR_PINE_SNOW: drawPine(sx, sy, scale, fog, true); break;
    case SPR_TREE:      drawTree(sx, sy, scale, fog); break;
    case SPR_PALM:      drawPalm(sx, sy, scale, fog); break;
    case SPR_BUSH: {
      float r = scale * 520.0f * SCR_CX;
      if (r < 1.5f) return;
      gfx_fillEllipse((int)sx, (int)(sy - r * 0.55f), (int)r, (int)(r * 0.6f), fogF(pal.foliageD, fog));
      gfx_fillEllipse((int)(sx + r * 0.25f), (int)(sy - r * 0.75f), (int)(r * 0.6f), (int)(r * 0.4f),
                      fogF(pal.foliageL, fog));
      break;
    }
    case SPR_ROCK: {
      float h = scale * 750.0f * SCR_CY, w = scale * 1100.0f * SCR_CX;
      if (h < 1.5f) return;
      uint16_t rc = fogF(pal.rock, fog);
      float v[12] = { sx - w * 0.5f, sy, sx - w * 0.4f, sy - h * 0.6f, sx - w * 0.1f, sy - h,
                      sx + w * 0.3f, sy - h * 0.85f, sx + w * 0.5f, sy - h * 0.3f, sx + w * 0.45f, sy };
      gfx_fillPoly(v, 6, darkenCol(rc, 0.7f));
      float u[8] = { sx - w * 0.38f, sy - h * 0.6f, sx - w * 0.1f, sy - h * 0.98f,
                     sx + w * 0.28f, sy - h * 0.83f, sx + w * 0.05f, sy - h * 0.5f };
      gfx_fillPoly(u, 4, rc);
      break;
    }
    case SPR_POST: {
      float h = scale * 330.0f * SCR_CY, w = fmaxF(0.5f, scale * 45.0f * SCR_CX);
      if (h < 2) return;
      gfx_fillQuad(sx - w, sy, sx + w, sy, sx + w, sy - h, sx - w, sy - h, fogF(rgb(230, 230, 230), fog));
      float rh = fmaxF(1.0f, h * 0.16f);
      gfx_fillQuad(sx - w, sy - h * 0.82f, sx + w, sy - h * 0.82f, sx + w, sy - h * 0.82f - rh,
                   sx - w, sy - h * 0.82f - rh, fogF(COL_RED, fog));
      if (pal.night > 0.3f && h > 5)   // the reflector catches the headlights
        gfx_glow((int)sx, (int)(sy - h * 0.82f), (int)fmaxF(2, rh * 1.5f), rgb(255, 60, 40),
                 (int)(22 * pal.night * (1 - fog)));
      break;
    }
    default: break;
  }
}

float spriteHitWidth(int type) {
  switch (type) {
    case SPR_PINE:      return 0.10f;
    case SPR_PINE_SNOW: return 0.10f;
    case SPR_TREE:      return 0.10f;
    case SPR_PALM:      return 0.08f;
    case SPR_ROCK:      return 0.32f;
    case SPR_POST:      return 0.05f;
    default:            return 0.0f;   // bushes and unknown types are soft
  }
}
