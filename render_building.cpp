/*
  ═══════════════════════════════════════════════════════════════
  BUILDING RENDERING IMPLEMENTATION
  Faces are shaded from the palette's sun direction, windows reflect
  the sky by day and light up at night, and everything fades into the
  horizon haze. Segment::style = BuildingType | variant << 4 is mapped
  to a facade colour and a window style here.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_building.h"
#include "gfx.h"
#include "colors.h"
#include "utils.h"
#include <math.h>

static inline uint16_t fogC(uint16_t c, float fog) {
  return blend565(pal.fog, c, (uint32_t)(fog * 32.0f + 0.5f));
}

// Window styles: 0 punched windows, 1 ribbon windows, 2 glass floor lines,
// 3 sparse windows + neon trim at night, 4 none (cliffs, walls),
// 5 shopfront (big lit ground floor), 6 small villa windows
enum { WIN_PUNCHED = 0, WIN_RIBBON, WIN_GLASS, WIN_SPARSE, WIN_NONE, WIN_SHOPS, WIN_VILLA };

struct BldLook { uint8_t facade, win, roof; };

static BldLook lookOf(uint8_t style) {
  const int v = bldVariant(style);
  BldLook L = { FAC_CONCRETE, WIN_PUNCHED, FAC_CONCRETE };
  switch (bldType(style)) {
    case BLD_OFFICE: L.facade = (v & 1) ? FAC_CREAM : FAC_CONCRETE; L.win = (v & 2) ? WIN_RIBBON : WIN_PUNCHED; break;
    case BLD_GLASS:  L.facade = (v & 1) ? FAC_TEAL : FAC_BLUEGLASS; L.win = WIN_GLASS; break;
    case BLD_BRICK:  L.facade = (v & 1) ? FAC_RUST : FAC_BRICK; L.win = WIN_PUNCHED; break;
    case BLD_STONE:  L.facade = (v & 1) ? FAC_CREAM : FAC_SANDSTONE; L.win = WIN_PUNCHED; break;
    case BLD_MODERN: L.facade = FAC_SLATE; L.win = WIN_SPARSE; break;
    case BLD_SHOPS:  L.facade = (v & 1) ? FAC_BRICK : FAC_CREAM; L.win = WIN_SHOPS; break;
    case BLD_VILLA:  L.facade = FAC_STUCCO; L.win = WIN_VILLA; break;
    case BLD_CLIFF:  L.facade = FAC_ROCK; L.win = WIN_NONE; break;
    case BLD_WALL:   L.facade = FAC_WALLGREY; L.win = WIN_NONE; break;
    default: break;
  }
  L.roof = (bldType(style) == BLD_VILLA) ? FAC_TERRACOTTA : L.facade;
  return L;
}

void drawBuilding(const ProjPt& pn, const ProjPt& pf, int heightUnits, uint8_t style,
                  int sIdx, bool isLeft, bool showFront, float fog) {
  const float H = (float)heightUnits * BUILDING_H_UNIT;
  const BldLook look = lookOf(style);
  const int facade = look.facade;
  const int winStyle = look.win;
  const float dir = isLeft ? -1.0f : 1.0f;
  const bool natural = winStyle == WIN_NONE;

  float sideN = pn.x + dir * pn.w * BUILDING_OFFSET;
  float sideF = pf.x + dir * pf.w * BUILDING_OFFSET;
  float outN  = sideN + dir * pn.scale * BUILDING_W;
  float outF  = sideF + dir * pf.scale * BUILDING_W;
  float gN = pn.y, gF = pf.y;
  float hN = pn.scale * H, hF = pf.scale * H;
  float topN = gN - hN, topF = gF - hF;

  // Reject slices entirely off screen
  float minX = fminF(fminF(sideN, sideF), fminF(outN, outF));
  float maxX = fmaxF(fmaxF(sideN, sideF), fmaxF(outN, outF));
  if (maxX < 0 || minX >= SCR_W) return;
  if (fminF(topN, topF) >= gfx_cy1) return;

  // The face towards the road is lit when the sun is on the road side of it
  bool sunFromRight = pal.sunDirX >= 0;
  bool sideLit = (isLeft == sunFromRight);
  uint16_t sideCol  = fogC(sideLit ? pal.facadeLit[facade] : pal.facadeShade[facade], fog);
  uint16_t frontCol = fogC(pal.facadeFront[facade], fog);
  uint16_t roofCol  = fogC(pal.roof[look.roof], fog);

  // 1. SIDE WALL (facing the road)
  gfx_fillQuad(sideN, gN, sideF, gF, sideF, topF, sideN, topN, sideCol);

  // Rock faces: a few darker strata so cliffs do not read as flat walls
  if (natural) {
    if (bldType(style) == BLD_CLIFF && hN > 6) {
      uint16_t strata = darkenCol(sideCol, 0.82f);
      for (int k = 1; k <= 3; k++) {
        uint32_t h = hash32((uint32_t)sIdx * 13u + (uint32_t)k * 101u + (isLeft ? 7u : 0u));
        float f = (0.2f + 0.22f * k) + ((h & 15) - 7.5f) * 0.008f;
        float th = 0.04f;
        gfx_fillQuad(sideN, gN - hN * f, sideF, gF - hF * f, sideF, gF - hF * (f + th), sideN, gN - hN * (f + th), strata);
      }
    } else if (bldType(style) == BLD_WALL && hN > 4) {
      uint16_t joint = darkenCol(sideCol, 0.75f);
      gfx_fillQuad(sideN, topN, sideF, topF, sideF, topF + hF * 0.08f, sideN, topN + hN * 0.08f, joint);
    }
  }

  // 2. WINDOWS on the side wall (LOD: only when a floor is >= 4 px tall)
  float floorPx = pn.scale * BUILDING_FLOOR_H;
  int floors = (int)(H / BUILDING_FLOOR_H);
  float night = pal.night;
  uint16_t dayWin = lerpCol(darkenCol(pal.windowDay, 0.8f), sideCol, 0.12f + 0.08f * (sIdx & 1));
  uint16_t litWin = pal.windowLit;
  uint16_t darkWin = darkenCol(sideCol, 0.45f);
  if (!natural && floorPx >= 4.0f && floors >= 1) {
    float t0 = 0.14f, t1 = 0.86f;
    if (winStyle == WIN_RIBBON || winStyle == WIN_GLASS) { t0 = 0.0f; t1 = 1.0f; }
    if (winStyle == WIN_VILLA) { t0 = 0.30f; t1 = 0.70f; }
    float xa = lerpF(sideN, sideF, t0), xb = lerpF(sideN, sideF, t1);
    float ga = lerpF(gN, gF, t0), gb = lerpF(gN, gF, t1);
    float sa = lerpF(pn.scale, pf.scale, t0), sb = lerpF(pn.scale, pf.scale, t1);
    for (int f = 0; f < floors; f++) {
      float hb = (f + 0.22f) * BUILDING_FLOOR_H, ht = (f + 0.82f) * BUILDING_FLOOR_H;
      if (winStyle == WIN_GLASS) { hb = (f + 0.92f) * BUILDING_FLOOR_H; ht = (f + 1.0f) * BUILDING_FLOOR_H; }
      if (winStyle == WIN_VILLA) { hb = (f + 0.35f) * BUILDING_FLOOR_H; ht = (f + 0.75f) * BUILDING_FLOOR_H; }
      if (winStyle == WIN_SHOPS && f == 0) { hb = 0.05f * BUILDING_FLOOR_H; ht = 0.80f * BUILDING_FLOOR_H; }
      float yab = ga - sa * hb, ybb = gb - sb * hb;
      float yat = ga - sa * ht, ybt = gb - sb * ht;
      if (fmaxF(yab, ybb) < 0) break;                      // above the screen
      if (fminF(yat, ybt) >= gfx_cy1) continue;            // hidden below
      uint32_t h = hash32((uint32_t)sIdx * 73u + (uint32_t)f * 7919u + (isLeft ? 1u : 0u));
      if (winStyle == WIN_SPARSE && (h & 1)) continue;
      if (winStyle == WIN_VILLA && (sIdx & 1)) continue;
      uint16_t wc;
      if (winStyle == WIN_GLASS) {
        wc = darkenCol(sideCol, 0.7f);                      // glass tower floor lines
      } else if (winStyle == WIN_SHOPS && f == 0) {
        // shop window: bright display at night, dark glass by day
        wc = lerpCol(rgb(40, 44, 52), lerpCol(litWin, COL_WHITE, 0.3f), 0.25f + 0.75f * night);
        wc = fogC(wc, fog);
      } else {
        bool lit = (h >> 3) % 100 < 55;
        uint16_t nightCol = lit ? litWin : darkWin;
        wc = lerpCol(dayWin, nightCol, night);
        wc = fogC(wc, fog * (lit ? (1.0f - 0.5f * night) : 1.0f));
      }
      gfx_fillQuad(xa, yab, xb, ybb, xb, ybt, xa, yat, wc);
    }
    // Shop awning over the ground floor
    if (winStyle == WIN_SHOPS) {
      static const uint16_t AWN[4] = { 0xC104, 0x0451, 0xDC40, 0x2A8B };
      uint16_t aw = fogC(AWN[bldVariant(style) & 3], fog);
      float ab = BUILDING_FLOOR_H * 0.80f, at = BUILDING_FLOOR_H * 0.95f;
      gfx_fillQuad(sideN, gN - pn.scale * ab, sideF, gF - pf.scale * ab,
                   sideF, gF - pf.scale * at, sideN, gN - pn.scale * at, aw);
    }
  }

  // Neon trim on modern towers at night
  if (winStyle == WIN_SPARSE && night > 0.3f) {
    uint16_t neon = (bldVariant(style) & 1) ? rgb(255, 40, 200) : rgb(40, 230, 255);
    float w = fmaxF(1.0f, pn.scale * 1500.0f);
    gfx_fillQuad(sideN, gN, sideN + dir * w, gN, sideN + dir * w, topN, sideN, topN,
                 neon, GFX_ADD, (int)(24 * night * (1.0f - fog)));
  }

  // 3. ROOF: only visible when it is below eye level
  if (topN > topF + 0.5f)
    gfx_fillQuad(sideN, topN, sideF, topF, outF, topF, outN, topN, roofCol);

  // 4. FRONT FACADE (first slice of the building)
  if (showFront) {
    gfx_fillQuad(sideN, gN, outN, gN, outN, topN, sideN, topN, natural ? sideCol : frontCol);
    float fx0 = fminF(sideN, outN), fx1 = fmaxF(sideN, outN);
    float fw = fx1 - fx0;
    if (!natural && floorPx >= 3.0f && fw > 12) {
      const int cols = (winStyle == WIN_VILLA) ? 2 : 4;
      uint16_t fDay = lerpCol(pal.windowDay, frontCol, 0.3f);
      uint16_t fDark = darkenCol(frontCol, 0.45f);
      for (int f = 0; f < floors; f++) {
        float yb = gN - (f + 0.30f) * floorPx, yt = gN - (f + 0.78f) * floorPx;
        if (yb < 0) break;
        if (yt >= gfx_cy1) continue;
        for (int c = 0; c < cols; c++) {
          float xa = fx0 + fw * (c + 0.2f) / cols, xb = fx0 + fw * (c + 0.8f) / cols;
          uint32_t h = hash32((uint32_t)sIdx * 31u + (uint32_t)f * 131u + (uint32_t)c);
          bool lit = (h >> 3) % 100 < 55;
          uint16_t wc = lerpCol(fDay, lit ? litWin : fDark, night);
          gfx_fillQuad(xa, yb, xb, yb, xb, yt, xa, yt, fogC(wc, fog));
        }
      }
      // Entrance
      float dw = fw * 0.28f, dh = fminF(floorPx * 0.9f, hN * 0.3f);
      float dx = fx0 + fw * 0.5f - dw * 0.5f;
      uint16_t doorCol = fogC(lerpCol(rgb(25, 25, 30), litWin, night * 0.6f), fog);
      gfx_fillQuad(dx, gN, dx + dw, gN, dx + dw, gN - dh, dx, gN - dh, doorCol);
    }
  }
}
