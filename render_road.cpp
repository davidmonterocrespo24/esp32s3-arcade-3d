/*
  ═══════════════════════════════════════════════════════════════
  ROAD, TUNNEL, BUILDING, SCENERY AND TRAFFIC RENDERING

  Pass 1 (front to back): project every segment boundary once and
  record, per segment, the screen row below which nearer road already
  covers everything (clipRow).
  Pass 2 (back to front): for each segment draw road rows, buildings,
  tunnel, scenery, traffic and the tunnel portal, clipped to clipRow,
  so hills hide what is behind them and nearer things paint over
  farther ones (painter's algorithm).

  Changes vs. the first version:
  - Road drawn per SCANLINE with exact edge interpolation and 1-px
    anti-aliased boundaries (was 3 stair-stepped rectangles/segment).
  - Fog per scanline from the true depth (1/z interpolation), applied to
    buildings, trees, lamps and traffic too (they used to ignore fog).
  - Sprites and traffic are drawn inside the back-to-front pass, so a
    tree behind a building is hidden by it (they used to be drawn last,
    on top of everything), and traffic is placed at its exact position
    inside the segment (it used to snap from segment to segment).
  - Real near-plane clipping instead of skipping segment 0.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_road.h"
#include "rendering.h"
#include "render_building.h"
#include "render_traffic.h"
#include "track.h"
#include "physics.h"
#include "utils.h"
#include <math.h>

static const float NEAR_Z = 30.0f;
static const float WALL_K = 1.20f;          // tunnel wall position (road half-widths)
static const float MAX_Z  = (float)DRAW_DIST * SEG_LEN;

struct SegView {
  ProjPt n, f;          // near (clipped) and far boundary projections
  float  nyW, fyW;      // world elevation at the near / far boundary
  float  nxW, fxW;      // world lateral offset of the road centre
  float  fog;           // 0..1 at mid-segment
  int    clipRow;       // rows >= clipRow are covered by nearer road
  bool   valid, roadVis;
};
static SegView sv[DRAW_DIST];

// Fog factor LUT over normalised depth (avoids expf per scanline)
#define FOG_LUT 64
static uint8_t sFogLUT[FOG_LUT + 1];
static bool sFogInit = false;

static void initFog() {
  for (int i = 0; i <= FOG_LUT; i++)
    sFogLUT[i] = (uint8_t)(expFog((float)i / FOG_LUT, FOG_DENSITY) * 32.0f + 0.5f);
  sFogInit = true;
}

static inline int fogAlpha(float z) {
  int i = (int)(z * (FOG_LUT / MAX_Z));
  if (i < 0) i = 0;
  if (i > FOG_LUT) i = FOG_LUT;
  return sFogLUT[i];
}

static inline uint16_t fogMix(uint16_t c, uint16_t fogc, int a32) {
  return blend565(fogc, c, (uint32_t)a32);
}

static inline uint16_t fogF(uint16_t c, float f) {
  return blend565(pal.fog, c, (uint32_t)(f * 32.0f + 0.5f));
}

static inline int rowOf(float y) {
  if (y < -1.0f) return 0;
  if (y > SCR_H + 1.0f) return SCR_H;
  int r = (int)ceilf(y - 0.5f);
  return iclamp(r, 0, SCR_H);
}

static float sCamX, sCamY, sCamDepth;

static inline void project(ProjPt& p, float wx, float wy, float wz) {
  p.z = wz;
  p.scale = sCamDepth / wz;
  p.x = SCR_CX + p.scale * (wx - sCamX) * SCR_CX;
  p.y = SCR_CY - p.scale * (wy - sCamY) * SCR_CY;
  p.w = p.scale * ROAD_W * SCR_CX;
}

// ═══════════════════════════════════════════════════════════════
//  ROAD ROWS
// ═══════════════════════════════════════════════════════════════
static void drawBand(const SegView& s, const Segment& seg, int sIdx) {
  int r0 = rowOf(s.f.y), r1 = imin(rowOf(s.n.y), s.clipRow);
  if (r1 <= r0) return;
  float dy = s.n.y - s.f.y;
  if (dy < 1e-3f) return;

  const bool light = ((sIdx / RUMBLE_LEN) & 1) == 0;
  const bool tun = seg.tunnel != 0;
  uint16_t bGrass, bRumble, bEdge, bRoad, bLane, fogc;
  if (tun) {
    bGrass  = light ? pal.tunnelWallL : pal.tunnelWallD;
    bRumble = light ? pal.tunnelWallD : pal.tunnelCeil;
    bEdge   = darkenCol(pal.edgeLine, 0.7f);
    bRoad   = darkenCol(light ? pal.roadL : pal.roadD, 0.72f);
    bLane   = darkenCol(pal.lane, 0.75f);
    fogc    = COL_BLACK;
  } else {
    bGrass  = light ? pal.grassL : pal.grassD;
    bRumble = light ? pal.rumbleL : pal.rumbleD;
    bEdge   = pal.edgeLine;
    bRoad   = light ? pal.roadL : pal.roadD;
    bLane   = pal.lane;
    fogc    = pal.fog;
  }

  float invZf = 1.0f / s.f.z, invZn = 1.0f / s.n.z;
  int lastA = -1;
  uint16_t g = 0, ru = 0, ed = 0, ro = 0, la = 0;
  float b[12];
  uint16_t c[13];

  for (int y = r0; y < r1; y++) {
    float t = (y + 0.5f - s.f.y) / dy;              // 0 far .. 1 near
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float cx = s.f.x + (s.n.x - s.f.x) * t;
    float hw = s.f.w + (s.n.w - s.f.w) * t;
    float z = 1.0f / (invZf + (invZn - invZf) * t);
    int a = fogAlpha(tun ? z * 1.6f : z);
    if (tun && a > 28) a = 28;
    if (a != lastA) {
      g = fogMix(bGrass, fogc, a); ru = fogMix(bRumble, fogc, a); ed = fogMix(bEdge, fogc, a);
      ro = fogMix(bRoad, fogc, a); la = fogMix(bLane, fogc, a);
      lastA = a;
    }
    float rw = hw * 0.11f, ew = hw * 0.035f, lw = hw * 0.03f;
    int k = 0, nc = 0;
    c[nc++] = g;   b[k++] = cx - hw - rw;
    c[nc++] = ru;  b[k++] = cx - hw;
    c[nc++] = ed;  b[k++] = cx - hw + ew;
    c[nc++] = ro;
    if (light) {
      for (int l = 1; l < LANES; l++) {
        float lx = cx - hw + 2.0f * hw * l / LANES;
        b[k++] = lx - lw * 0.5f; c[nc++] = la;
        b[k++] = lx + lw * 0.5f; c[nc++] = ro;
      }
    }
    b[k++] = cx + hw - ew; c[nc++] = ed;
    b[k++] = cx + hw;      c[nc++] = ru;
    b[k++] = cx + hw + rw; c[nc++] = g;
    gfx_rowRegions(y, b, c, nc);
  }
}

// ═══════════════════════════════════════════════════════════════
//  TUNNEL
// ═══════════════════════════════════════════════════════════════
static inline float ceilY(const ProjPt& p, float worldY) {
  return SCR_CY - p.scale * (worldY + TUNNEL_H - sCamY) * SCR_CY;
}

static void drawTunnelSeg(const SegView& s, int sIdx, bool farthest) {
  float cyN = ceilY(s.n, s.nyW), cyF = ceilY(s.f, s.fyW);
  float lN = s.n.x - s.n.w * WALL_K, rN = s.n.x + s.n.w * WALL_K;
  float lF = s.f.x - s.f.w * WALL_K, rF = s.f.x + s.f.w * WALL_K;
  float gN = s.n.y, gF = s.f.y;
  float dark = fminF(s.fog * 1.4f, 0.92f);   // the tunnel fades to black, not to haze

  if (farthest)   // tunnel continues past the draw distance: dark opening
    gfx_fillQuad(lF, cyF, rF, cyF, rF, gF, lF, gF, COL_BLACK);

  bool light = ((sIdx / 2) & 1) == 0;
  uint16_t wall = darkenCol(light ? pal.tunnelWallL : pal.tunnelWallD, 1.0f - dark);
  uint16_t ceil = darkenCol(pal.tunnelCeil, 1.0f - dark);
  gfx_fillQuad(lN, gN, lF, gF, lF, cyF, lN, cyN, wall);
  gfx_fillQuad(rN, gN, rN, cyN, rF, cyF, rF, gF, wall);
  gfx_fillQuad(lN, cyN, rN, cyN, rF, cyF, lF, cyF, ceil);

  // Dark skirting strip along the bottom of each wall
  float kh = TUNNEL_H * 0.08f;
  float kN = s.n.scale * kh * SCR_CY, kF = s.f.scale * kh * SCR_CY;
  uint16_t skirt = darkenCol(wall, 0.55f);
  gfx_fillQuad(lN, gN, lF, gF, lF, gF - kF, lN, gN - kN, skirt);
  gfx_fillQuad(rN, gN, rN, gN - kN, rF, gF - kF, rF, gF, skirt);

  // Ceiling lights every 4 segments: lamp strip + light pool on the road
  if ((sIdx % 4) == 0) {
    float mN = s.n.x, mF = s.f.x, wN = s.n.w * 0.10f, wF = s.f.w * 0.10f;
    uint16_t lamp = lerpCol(pal.tunnelLight, COL_BLACK, dark * 0.8f);
    gfx_fillQuad(mN - wN, cyN, mN + wN, cyN, mF + wF, cyF, mF - wF, cyF, lamp);
    int pa = (int)(10 * (1.0f - dark));
    if (pa > 0) {
      int py = (int)(gN * 0.6f + gF * 0.4f);
      gfx_fillEllipse((int)(mN * 0.6f + mF * 0.4f), py, (int)(s.n.w * 0.8f),
                      (int)fmaxF(1.0f, (gN - gF) * 0.6f), pal.tunnelLight, GFX_ADD, pa);
    }
  }
}

// Concrete face around the tunnel mouth, with grass on top, at the near
// boundary of the first tunnel segment.
static void drawPortal(const SegView& s) {
  float sc = s.n.scale, gy = s.n.y;
  float cy = ceilY(s.n, s.nyW);
  float topY = SCR_CY - sc * (s.nyW + TUNNEL_H * 2.1f - sCamY) * SCR_CY;
  float l = s.n.x - s.n.w * WALL_K, r = s.n.x + s.n.w * WALL_K;
  float halfBase = s.n.w * 7.5f, halfTop = s.n.w * 3.2f;
  uint16_t col = fogF(pal.portal, s.fog);
  uint16_t grass = fogF(pal.foliageD, s.fog);
  uint16_t frame = darkenCol(col, 0.72f);
  float grassLine = topY + (gy - topY) * 0.12f;

  int y0 = rowOf(topY), y1 = imin(rowOf(gy), gfx_cy1);
  float span = gy - topY;
  if (span < 1) return;
  for (int y = y0; y < y1; y++) {
    float yc = y + 0.5f;
    float t = (yc - topY) / span;
    float hw = halfTop + (halfBase - halfTop) * t;
    int xa = (int)ceilf(s.n.x - hw - 0.5f), xb = (int)ceilf(s.n.x + hw - 0.5f);
    uint16_t cc = (yc < grassLine) ? grass : col;
    if (yc >= cy) {
      gfx_hspan(y, xa, (int)ceilf(l - 0.5f), cc);
      gfx_hspan(y, (int)ceilf(r - 0.5f), xb, cc);
    } else {
      gfx_hspan(y, xa, xb, cc);
    }
  }
  float fw = s.n.w * 0.14f;
  gfx_fillQuad(l - fw, cy - fw, r + fw, cy - fw, r + fw, cy, l - fw, cy, frame);
  gfx_fillQuad(l - fw, cy, l, cy, l, gy, l - fw, gy, frame);
  gfx_fillQuad(r, cy, r + fw, cy, r + fw, gy, r, gy, frame);
}

// ═══════════════════════════════════════════════════════════════
//  SCENERY SPRITES
// ═══════════════════════════════════════════════════════════════
void drawSceneSprite(int type, float sx, float sy, float scale, float fog) {
  if (sx < -80 || sx > SCR_W + 80) return;
  float day = 1.0f - pal.night;
  switch (type) {
    case SPR_PINE: {
      float h = scale * 30000, w = scale * 11000;
      if (h < 3) return;
      if (day > 0.1f) gfx_fillEllipse((int)sx, (int)sy, (int)(w * 0.6f), (int)fmaxF(1, w * 0.12f), COL_BLACK, GFX_BLEND, (int)(8 * day * (1 - fog)));
      float tw = fmaxF(1.5f, w * 0.14f), th = h * 0.22f;
      gfx_fillQuad(sx - tw * 0.5f, sy, sx + tw * 0.5f, sy, sx + tw * 0.5f, sy - th, sx - tw * 0.5f, sy - th, fogF(pal.trunk, fog));
      uint16_t dk = fogF(pal.foliageD, fog), lt = fogF(pal.foliageL, fog);
      for (int i = 0; i < 3; i++) {   // three tiers, shaded halves
        float base = sy - th - h * 0.22f * i, top = base - h * 0.42f, half = w * (0.5f - 0.11f * i);
        gfx_fillTri(sx, top, sx - half, base, sx, base, dk);
        gfx_fillTri(sx, top, sx, base, sx + half, base, lt);
      }
      break;
    }
    case SPR_TREE: {
      float h = scale * 27000, w = scale * 13000;
      if (h < 3) return;
      if (day > 0.1f) gfx_fillEllipse((int)sx, (int)sy, (int)(w * 0.55f), (int)fmaxF(1, w * 0.12f), COL_BLACK, GFX_BLEND, (int)(8 * day * (1 - fog)));
      float tw = fmaxF(1.5f, w * 0.12f), th = h * 0.5f;
      gfx_fillQuad(sx - tw * 0.5f, sy, sx + tw * 0.5f, sy, sx + tw * 0.4f, sy - th, sx - tw * 0.4f, sy - th, fogF(pal.trunk, fog));
      float r = w * 0.5f;
      int cyc = (int)(sy - th - r * 0.6f);
      gfx_fillEllipse((int)sx, cyc, (int)r, (int)(r * 0.9f), fogF(pal.foliageD, fog));
      gfx_fillEllipse((int)(sx + r * 0.2f), (int)(cyc - r * 0.2f), (int)(r * 0.7f), (int)(r * 0.62f), fogF(pal.foliageL, fog));
      break;
    }
    case SPR_BUSH: {
      float r = scale * 6500;
      if (r < 1.5f) return;
      gfx_fillEllipse((int)sx, (int)(sy - r * 0.55f), (int)r, (int)(r * 0.6f), fogF(pal.foliageD, fog));
      gfx_fillEllipse((int)(sx + r * 0.25f), (int)(sy - r * 0.75f), (int)(r * 0.6f), (int)(r * 0.4f), fogF(pal.foliageL, fog));
      break;
    }
    case SPR_ROCK: {
      float h = scale * 6000, w = scale * 9000;
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
      float h = scale * 9000, w = fmaxF(1.0f, scale * 900);
      if (h < 2) return;
      gfx_fillQuad(sx - w, sy, sx + w, sy, sx + w, sy - h, sx - w, sy - h, fogF(rgb(230, 230, 230), fog));
      float rh = fmaxF(1.0f, h * 0.16f);
      gfx_fillQuad(sx - w, sy - h * 0.82f, sx + w, sy - h * 0.82f, sx + w, sy - h * 0.82f - rh, sx - w, sy - h * 0.82f - rh, fogF(COL_RED, fog));
      if (pal.night > 0.3f && scale > 0.00015f)   // reflector catches the headlights
        gfx_glow((int)sx, (int)(sy - h * 0.82f), (int)fmaxF(2, rh * 1.5f), rgb(255, 60, 40), (int)(22 * pal.night * (1 - fog)));
      break;
    }
  }
}

// Street lamp on one side of the road, with its light at night.
static void drawLamp(const ProjPt& p, int side, float fog) {
  float sc = p.scale;
  float baseX = p.x + side * p.w * 1.28f;
  float poleH = sc * 3600.0f * SCR_CY;
  if (poleH < 3) return;
  float pw = fmaxF(1.0f, sc * 70.0f * SCR_CX);
  float topY = p.y - poleH;
  float armX = baseX - side * p.w * 0.30f;
  uint16_t pole = fogF(pal.lampPole, fog);
  gfx_fillQuad(baseX - pw * 0.5f, p.y, baseX + pw * 0.5f, p.y, baseX + pw * 0.5f, topY, baseX - pw * 0.5f, topY, pole);
  float ah = fmaxF(1.0f, pw * 0.7f);
  gfx_fillQuad(baseX, topY, armX, topY + ah * 0.5f, armX, topY + ah * 1.5f, baseX, topY + ah, pole);
  float hw = fmaxF(1.5f, pw * 1.6f);
  uint16_t head = lerpCol(fogF(rgb(200, 200, 190), fog), pal.lampLight, pal.night);
  gfx_fillQuad(armX - hw, topY + ah * 1.5f, armX + hw, topY + ah * 1.5f, armX + hw * 0.7f, topY + ah * 2.6f, armX - hw * 0.7f, topY + ah * 2.6f, head);
  if (pal.night > 0.15f) {
    int a = (int)(26 * pal.night * (1.0f - fog * 0.7f));
    gfx_glow((int)armX, (int)(topY + ah * 2.2f), (int)fminF(28.0f, fmaxF(3, hw * 3.0f)), pal.lampLight, a);
    // light pool on the road under the lamp
    gfx_fillEllipse((int)armX, (int)p.y, (int)(p.w * 0.55f), (int)fmaxF(1, p.w * 0.07f),
                    pal.lampLight, GFX_ADD, (int)(6 * pal.night * (1.0f - fog)));
  }
}

// ═══════════════════════════════════════════════════════════════
//  MAIN PASS
// ═══════════════════════════════════════════════════════════════
struct CarSlot { int n; int car; float frac; };

void drawRoad(float position, float playerX, float playerZdist, float cameraDepth) {
  if (!sFogInit) initFog();
  int baseIdx = findSegIdx(position);
  float posOff = position - floorf(position / SEG_LEN) * SEG_LEN;
  float basePct = posOff / SEG_LEN;

  int pSeg = findSegIdx(position + playerZdist);
  int pPrev = (pSeg - 1 + TOTAL_SEGS) % TOTAL_SEGS;
  float pPct = percentRemaining(position + playerZdist, SEG_LEN);
  float playerY = lerpF(segments[pPrev].y, segments[pSeg].y, pPct);
  sCamY = playerY + CAM_HEIGHT;
  sCamX = playerX * ROAD_W;
  sCamDepth = cameraDepth;

  // World data of every boundary: centre offset (curve accumulation),
  // elevation (far end of the previous segment) and camera-space z.
  static float bx[DRAW_DIST + 1], by[DRAW_DIST + 1], bz[DRAW_DIST + 1];
  {
    float x = 0, dx = -(segments[baseIdx].curve * basePct);
    for (int n = 0; n <= DRAW_DIST; n++) {
      bx[n] = x;
      by[n] = segments[(baseIdx + n - 1 + TOTAL_SEGS) % TOTAL_SEGS].y;
      bz[n] = n * SEG_LEN - posOff;
      if (n < DRAW_DIST) { x += dx; dx += segments[(baseIdx + n) % TOTAL_SEGS].curve; }
    }
  }

  // ---- Pass 1: front to back ----
  float maxy = SCR_H;
  int lastValid = -1;
  for (int n = 0; n < DRAW_DIST; n++) {
    SegView& s = sv[n];
    s.clipRow = rowOf(maxy);
    s.roadVis = false;
    s.valid = bz[n + 1] > NEAR_Z;
    if (!s.valid) continue;
    lastValid = n;
    float nz = bz[n], nx = bx[n], ny = by[n];
    if (nz < NEAR_Z) {    // clip the near end to the near plane
      float t = (NEAR_Z - nz) / (bz[n + 1] - nz);
      nx += (bx[n + 1] - bx[n]) * t;
      ny += (by[n + 1] - by[n]) * t;
      nz = NEAR_Z;
    }
    project(s.n, nx, ny, nz);
    project(s.f, bx[n + 1], by[n + 1], bz[n + 1]);
    s.nyW = ny; s.fyW = by[n + 1];
    s.nxW = nx; s.fxW = bx[n + 1];
    s.fog = sFogLUT[iclamp((int)((nz + bz[n + 1]) * 0.5f * (FOG_LUT / MAX_Z)), 0, FOG_LUT)] / 32.0f;
    float top = s.f.y, bot = fminF(s.n.y, maxy);
    if (top < bot) { s.roadVis = true; maxy = top; }
  }

  // Ground beyond the draw distance (below the horizon, above the road)
  int horizonRow = SCR_CY;
  int groundEnd = rowOf(maxy);
  for (int y = horizonRow; y < groundEnd; y++) gfx_hspan(y, 0, SCR_W, pal.fog);

  // Traffic per segment (exact position inside the segment)
  CarSlot slots[MAX_CARS];
  int nSlots = 0;
  for (int i = 0; i < MAX_CARS; i++) {
    float rel = trafficCars[i].z - (position - posOff);   // from the base boundary
    if (rel < 0) rel += trackLength;
    if (rel >= trackLength) rel -= trackLength;
    int n = (int)(rel / SEG_LEN);
    if (n < 1 || n >= DRAW_DIST) continue;
    slots[nSlots].n = n;
    slots[nSlots].car = i;
    slots[nSlots].frac = (rel - n * SEG_LEN) / SEG_LEN;
    nSlots++;
  }
  // far to near
  for (int i = 1; i < nSlots; i++) {
    CarSlot k = slots[i];
    int j = i - 1;
    while (j >= 0 && (slots[j].n + slots[j].frac) < (k.n + k.frac)) { slots[j + 1] = slots[j]; j--; }
    slots[j + 1] = k;
  }
  int slotIdx = 0;

  // ---- Pass 2: back to front ----
  for (int n = DRAW_DIST - 1; n >= 0; n--) {
    SegView& s = sv[n];
    if (!s.valid) continue;
    int sIdx = (baseIdx + n) % TOTAL_SEGS;
    const Segment& seg = segments[sIdx];
    const Segment& prevSeg = segments[(sIdx - 1 + TOTAL_SEGS) % TOTAL_SEGS];
    gfx_setClip(0, 0, SCR_W, s.clipRow);

    if (s.roadVis) drawBand(s, seg, sIdx);

    if (seg.tunnel) {
      drawTunnelSeg(s, sIdx, n == lastValid);
    } else {
      if (seg.buildL) {
        bool front = prevSeg.buildL != seg.buildL || prevSeg.styleL != seg.styleL || prevSeg.tunnel;
        drawBuilding(s.n, s.f, seg.buildL, seg.styleL, sIdx, true, front, s.fog);
      }
      if (seg.buildR) {
        bool front = prevSeg.buildR != seg.buildR || prevSeg.styleR != seg.styleR || prevSeg.tunnel;
        drawBuilding(s.n, s.f, seg.buildR, seg.styleR, sIdx, false, front, s.fog);
      }
    }

    if (n >= 1 && s.n.z > NEAR_Z) {
      if (seg.spriteType >= 0) {
        float sx = s.n.x + s.n.scale * seg.spriteOffset() * ROAD_W * SCR_CX;
        drawSceneSprite(seg.spriteType, sx, s.n.y, s.n.scale, s.fog);
      }
      if (seg.lamp) drawLamp(s.n, seg.lamp == 1 ? -1 : +1, s.fog);
    }

    // Traffic inside this segment
    while (slotIdx < nSlots && slots[slotIdx].n > n) slotIdx++;
    while (slotIdx < nSlots && slots[slotIdx].n == n) {
      const CarSlot& cs = slots[slotIdx++];
      const TrafficCar& car = trafficCars[cs.car];
      float wz = bz[n] + cs.frac * SEG_LEN;
      if (wz <= NEAR_Z * 4) continue;
      float wy = lerpF(by[n], by[n + 1], cs.frac);
      float wxC = lerpF(bx[n], bx[n + 1], cs.frac);
      float wx = wxC + car.offset * ROAD_W;
      ProjPt p;
      project(p, wx, wy, wz);
      float heading = atanf((bx[n + 1] - bx[n]) / SEG_LEN);
      // 0.7: the road projection stretches x by 4:3, the full angle overdoes it
      float view = atan2f(wx - sCamX, wz) * 0.7f;
      float pitch = atan2f(sCamY - wy, wz) + atanf((by[n + 1] - by[n]) / SEG_LEN);
      drawTrafficCar(p.x, p.y, p.scale, car.color, s.fog, heading - view, pitch,
                     seg.tunnel != 0, wz / 234.0f);
    }

    if (seg.tunnel && !prevSeg.tunnel && n >= 1) drawPortal(s);
  }
  gfx_resetClip();
}
