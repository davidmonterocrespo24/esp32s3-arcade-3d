/*
  ═══════════════════════════════════════════════════════════════
  BUILDING RENDERING IMPLEMENTATION
  Faces are shaded from the palette's sun direction, windows reflect
  the sky by day and light up at night, and everything fades into the
  horizon haze (the old buildings had no fog and popped in at full
  colour at the end of the draw distance).
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_building.h"
#include "rendering.h"
#include "utils.h"
#include <math.h>

static inline uint16_t fogC(uint16_t c, float fog) {
  return blend565(pal.fog, c, (uint32_t)(fog * 32.0f + 0.5f));
}

void drawBuilding(const ProjPt& pn, const ProjPt& pf, int heightUnits, uint8_t style,
                  int sIdx, bool isLeft, bool showFront, float fog) {
  const float H = (float)heightUnits * BUILDING_H_UNIT;
  const int facade = style & 7;
  const int winStyle = (style >> 3) & 3;
  const float dir = isLeft ? -1.0f : 1.0f;

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
  uint16_t roofCol  = fogC(pal.roof[facade], fog);

  // 1. SIDE WALL (facing the road)
  gfx_fillQuad(sideN, gN, sideF, gF, sideF, topF, sideN, topN, sideCol);

  // 2. WINDOWS on the side wall (LOD: only when a floor is >= 3 px tall)
  float floorPx = pn.scale * BUILDING_FLOOR_H;
  int floors = (int)(H / BUILDING_FLOOR_H);
  float night = pal.night;
  uint16_t dayWin = lerpCol(darkenCol(pal.windowDay, 0.8f), sideCol, 0.12f + 0.08f * (sIdx & 1));
  uint16_t litWin = pal.windowLit;
  uint16_t darkWin = darkenCol(sideCol, 0.45f);
  if (floorPx >= 4.0f && floors > 1) {
    float t0 = 0.14f, t1 = 0.86f;
    if (winStyle == 1) { t0 = 0.0f; t1 = 1.0f; }          // ribbon windows
    float xa = lerpF(sideN, sideF, t0), xb = lerpF(sideN, sideF, t1);
    float ga = lerpF(gN, gF, t0), gb = lerpF(gN, gF, t1);
    float sa = lerpF(pn.scale, pf.scale, t0), sb = lerpF(pn.scale, pf.scale, t1);
    for (int f = 0; f < floors; f++) {
      float hb = (f + 0.22f) * BUILDING_FLOOR_H, ht = (f + 0.82f) * BUILDING_FLOOR_H;
      if (winStyle == 2) { hb = (f + 0.92f) * BUILDING_FLOOR_H; ht = (f + 1.0f) * BUILDING_FLOOR_H; }
      float yab = ga - sa * hb, ybb = gb - sb * hb;
      float yat = ga - sa * ht, ybt = gb - sb * ht;
      if (fmaxF(yab, ybb) < 0) break;                      // above the screen
      if (fminF(yat, ybt) >= gfx_cy1) continue;            // hidden below
      uint32_t h = hash32((uint32_t)sIdx * 73u + (uint32_t)f * 7919u + (isLeft ? 1u : 0u));
      if (winStyle == 3 && (h & 1)) continue;              // sparse
      uint16_t wc;
      if (winStyle == 2) {
        wc = darkenCol(sideCol, 0.7f);                      // glass tower floor lines
      } else {
        bool lit = (h >> 3) % 100 < 55;
        uint16_t nightCol = lit ? litWin : darkWin;
        wc = lerpCol(dayWin, nightCol, night);
        wc = fogC(wc, fog * (lit ? (1.0f - 0.5f * night) : 1.0f));
      }
      gfx_fillQuad(xa, yab, xb, ybb, xb, ybt, xa, yat, wc);
    }
  }

  // Neon trim on some towers at night
  if (winStyle == 3 && night > 0.3f) {
    uint16_t neon = (facade & 1) ? rgb(255, 40, 200) : rgb(40, 230, 255);
    float w = fmaxF(1.0f, pn.scale * 1500.0f);
    gfx_fillQuad(sideN, gN, sideN + dir * w, gN, sideN + dir * w, topN, sideN, topN,
                 neon, GFX_ADD, (int)(24 * night * (1.0f - fog)));
  }

  // 3. ROOF: only visible when it is below eye level
  if (topN > topF + 0.5f)
    gfx_fillQuad(sideN, topN, sideF, topF, outF, topF, outN, topN, roofCol);

  // 4. FRONT FACADE (first slice of the building)
  if (showFront) {
    gfx_fillQuad(sideN, gN, outN, gN, outN, topN, sideN, topN, frontCol);
    float fx0 = fminF(sideN, outN), fx1 = fmaxF(sideN, outN);
    float fw = fx1 - fx0;
    if (floorPx >= 3.0f && fw > 12) {
      const int cols = 4;
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
