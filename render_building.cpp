/*
  ═══════════════════════════════════════════════════════════════
  BUILDING RENDERING IMPLEMENTATION

  Side walls are textured per screen column: u (along the road) is
  perspective-correct (u/z and 1/z interpolated across the slice), v is
  linear down the column (a vertical wall has constant depth per
  column). Each column walks three bands of the wall texture: CAP (top
  rows of the cap module), BODY (one floor per period, window slots
  re-coloured per window cell: day reflections or lights at night) and
  BASE (ground floor, uplit at night). Mip level per column from the
  floor height in pixels; at grazing angles the "averaged" column is
  used (floors read as bands instead of crawling windows); below ~1.3 px
  per floor a flat LOD colour. Front facades (constant depth) use the
  same column walker with affine u. Roofs are drawn when below the eye.

  Painter's order and the road pass's clipRow clip are respected: the
  whole slice is drawn inside the clip rectangle set by drawRoad().
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_building.h"
#include "textures.h"
#include "gfx.h"
#include "colors.h"
#include "circuits.h"
#include "utils.h"
#include <math.h>
#include <string.h>

static inline uint16_t fogC(uint16_t c, float fog) {
  return blend565(pal.fog, c, (uint32_t)(fog * 32.0f + 0.5f));
}

// ═══════════════════════════════════════════════════════════════
//  STYLE -> LOOK
// ═══════════════════════════════════════════════════════════════
enum RoofKind : uint8_t { ROOF_FLAT = 0, ROOF_PITCHED, ROOF_NATURAL };

struct Look {
  uint8_t tex;        // WallTexId
  uint8_t colors;     // colour variant 0..3
  uint8_t litPct;     // % of windows lit at night
  uint8_t roof;       // RoofKind
  bool    windows;    // window cells re-coloured (day looks / night lights)
  bool    curtain;    // glass curtain wall: day look graded by height
  bool    floored;    // body period = one floor
  bool    jagged;     // natural rock: jagged top edge
  bool    neon;       // crown / sign glow at night
};

static uint8_t cliffTint(int v) {
  static const uint8_t COAST[4] = { 0, 3, 0, 2 }, MOUNTAIN[4] = { 1, 2, 1, 1 };
  if (colorTheme == THEME_COAST) return COAST[v & 3];
  if (colorTheme == THEME_MOUNTAIN) return MOUNTAIN[v & 3];
  return 1;
}

static Look lookOf(uint8_t style) {
  const int v = bldVariant(style);
  Look L = { WT_OFFICE, (uint8_t)(v & 3), 45, ROOF_FLAT, true, false, true, false, false };
  switch (bldType(style)) {
    case BLD_OFFICE: L.tex = (v & 4) ? WT_RIBBON : WT_OFFICE; L.litPct = (uint8_t)(26 + (v >> 3) * 20); break;
    case BLD_GLASS:  L.tex = WT_GLASS; L.curtain = true; L.litPct = (uint8_t)(22 + (v >> 2) * 7); break;
    case BLD_BRICK:  L.tex = WT_BRICK; L.litPct = (uint8_t)(32 + (v >> 2) * 6); break;
    case BLD_STONE:  L.tex = WT_STONE; L.litPct = (uint8_t)(28 + (v >> 2) * 6); break;
    case BLD_MODERN: L.tex = WT_MODERN; L.litPct = (uint8_t)(20 + (v >> 2) * 6); L.neon = true; break;
    case BLD_SHOPS:  L.tex = WT_SHOPS; L.litPct = (uint8_t)(38 + (v >> 2) * 6); L.neon = true; break;
    case BLD_VILLA:  L.tex = WT_VILLA; L.litPct = 40; L.roof = ROOF_PITCHED; break;
    case BLD_CLIFF:  L.tex = WT_CLIFF; L.colors = cliffTint(v); L.windows = false; L.floored = false;
                     L.jagged = true; L.roof = ROOF_NATURAL; break;
    case BLD_WALL:   L.tex = WT_WALL; L.windows = false; L.floored = false; L.roof = ROOF_NATURAL; break;
    default: break;
  }
  return L;
}

// ═══════════════════════════════════════════════════════════════
//  TEXTURED WALL (column walker)
// ═══════════════════════════════════════════════════════════════
// ceil(y - 0.5) without a libm call: the pixel-centre rule of gfx
static inline int rowAt(float y) {
  float t = y - 0.5f;
  int i = (int)t;
  return i + (t > (float)i ? 1 : 0);
}

// Smooth 1-D noise in [0, 1) along the wall (jagged rock tops)
static inline float jagNoise(float u) {
  float x = u * (1.0f / 12.0f);
  int i = (int)floorf(x);
  float f = x - i;
  f = f * f * (3 - 2 * f);
  float a = (hash32((uint32_t)i * 0x9E3779B1u + 0x51u) >> 8) * (1.0f / 16777216.0f);
  float b = (hash32((uint32_t)(i + 1) * 0x9E3779B1u + 0x51u) >> 8) * (1.0f / 16777216.0f);
  float y = u * (1.0f / 4.0f);
  int j = (int)floorf(y);
  float g = (hash32((uint32_t)j * 0x85EBCA77u + 0x52u) >> 8) * (1.0f / 16777216.0f);
  return (a + (b - a) * f) * 0.8f + g * 0.2f;
}

#define MAX_FLOORS 64
#define WALL_BLOCK 8          // columns per exact perspective evaluation

void drawWallColumns(const WallJob& J) {
  const float x0 = fminF(J.xa, J.xb), x1 = fmaxF(J.xa, J.xb);
  const int c0 = imax(rowAt(x0), gfx_cx0), c1 = imin(rowAt(x1), gfx_cx1);
  const float dx = J.xb - J.xa;
  if (c1 <= c0 || fabsf(dx) < 1e-3f || J.vTot <= 0) return;
  const float invDx = 1.0f / dx;
  const WallTex& T = *J.tex;
  const int W0 = 1 << T.wLog2;
  int bodyLog2 = 0;
  while ((1 << bodyLog2) < T.body) bodyLog2++;
  uint16_t pal[TS_COUNT], palB[TS_COUNT];
  memcpy(pal, J.pal, sizeof(pal));
  memcpy(palB, J.palBase, sizeof(palB));
  const float dIz = J.izb - J.iza, dUz = J.uzb - J.uza;
  const float invVTot = 1.0f / J.vTot;
  const float invP = 1.0f / (float)T.body;
  static const float SC[TEX_LEVELS] = { 65536.0f, 32768.0f, 16384.0f, 8192.0f };
  // window colours per body floor: computed for the floors a column shows,
  // kept while the window cell stays the same (adjacent columns)
  const int nBody = J.windows ? iclamp(J.floors - 1, 0, MAX_FLOORS) : 0;
  const uint16_t* win[MAX_FLOORS];
  int winCell = -0x7FFFFFFF, winLo = 0, winHi = 0;
  bool baseDone = false;
  for (int k = 0; k < nBody; k++) win[k] = J.day[0];   // never an invalid pointer

  // ground / top lines step linearly per column; u and texels-per-pixel are
  // evaluated exactly every WALL_BLOCK columns and interpolated in between
  const float dGy = (J.gyb - J.gya) * invDx, dTy = (J.tyb - J.tya) * invDx;
  float gy = J.gya + (c0 + 0.5f - J.xa) * dGy, ty0 = J.tya + (c0 + 0.5f - J.xa) * dTy;
  float u = 0, du = 0, tpp = 0, dtpp = 0;
  int blk = 0, shift = T.winShift;
  float dudx = 0;

  for (int x = c0; x < c1; x++, gy += dGy, ty0 += dTy) {
    if (blk == 0) {
      const int n = imin(WALL_BLOCK, c1 - x);
      const float ta = clampF((x + 0.5f - J.xa) * invDx, 0.0f, 1.0f);
      const float tb = clampF((x + n + 0.5f - J.xa) * invDx, 0.0f, 1.0f);
      const float iza = J.iza + dIz * ta, izb = J.iza + dIz * tb;
      if (iza <= 0 || izb <= 0) { blk = 0; continue; }
      const float ua = (J.uza + dUz * ta) / iza, ub = (J.uza + dUz * tb) / izb;
      const float invN = 1.0f / (float)n;
      u = ua;
      du = (ub - ua) * invN;
      // texels per pixel vertically at both block ends (h is linear in x)
      const float ha = fmaxF(0.25f, gy - ty0), hb = fmaxF(0.25f, (gy + dGy * n) - (ty0 + dTy * n));
      tpp = J.vTot / ha;
      dtpp = (J.vTot / hb - tpp) * invN;
      dudx = fabsf(du);
      // window cell: coarser cells when a column covers several (stable at
      // grazing angles instead of flickering)
      shift = T.winShift;
      while (shift < 12 && dudx * 2.0f > (float)(1 << shift)) shift++;
      blk = n;
    }
    const float uc = u, tppc = tpp;
    u += du; tpp += dtpp; blk--;

    float ty = ty0;
    const float h = gy - ty;
    if (h < 0.5f) continue;
    float ppt = h * invVTot;                 // pixels per texel, vertically
    float vTot = J.vTot;
    if (J.jag > 0) {                         // lower the top edge by a noise profile
      float off = jagNoise(uc) * J.jag;
      if (off >= vTot - 1) continue;
      ty += off * ppt;
      vTot -= off;
    }
    const int ya = imax(rowAt(ty), gfx_cy0), yb = imin(rowAt(gy), gfx_cy1);
    if (yb <= ya) continue;
    if (tppc > 12.8f) {                      // farther than ~1.3 px per floor: flat LOD
      gfx_vspan(x, ya, yb, J.farCol);
      continue;
    }
    int l = tppc > 1.6f ? (tppc > 3.2f ? (tppc > 6.4f ? 3 : 2) : 1) : 0;
    const bool grazing = dudx > 3.0f * (float)(1 << l);
    const int R = T.rows >> l;
    const int ui = (int)uc;
    const uint8_t* colp = grazing ? T.avg[l] : T.lev[l] + (((ui >> l) & ((W0 >> l) - 1)) * R);
    const float e1 = fminF(J.capV, vTot), e2 = fmaxF(e1, vTot - J.baseV);
    const float sc = SC[l];
    const int32_t dv = (int32_t)(tppc * sc);

    const int cell = ui >> shift;
    if (J.windows && cell != winCell) { winCell = cell; winLo = winHi = 0; baseDone = false; }

    // band rows: texel w (from the top edge) is at screen y = ty + w * ppt
    const int yE1 = imin(yb, rowAt(ty + e1 * ppt)), yE2 = imin(yb, rowAt(ty + e2 * ppt));
    const int yB0 = imax(ya, yE1);
    if (nBody > 0 && yE2 > yB0) {
      // body floors this column shows: [k0, k1), with one floor of margin
      // on both ends (the fixed-point walk may round across a boundary)
      const int k0 = iclamp((int)(((yB0 + 0.5f - ty) * tppc - e1) * invP) - 1, 0, nBody - 1);
      const int k1 = iclamp((int)(((yE2 - 0.5f - ty) * tppc - e1) * invP) + 2, k0 + 1, nBody);
      if (k0 < winLo || k1 > winHi || winLo == winHi) {
        const bool had = winLo != winHi;
        const int lo = had ? imin(k0, winLo) : k0, hi = had ? imax(k1, winHi) : k1;
        for (int k = lo; k < hi; k++) {
          if (had && k >= winLo && k < winHi) continue;
          const int fl = J.floors - 1 - k;     // floor number from the ground
          const int ds = J.dayState == -2 ? iclamp(fl * TEX_WIN_STATES / imax(1, J.floors), 0, 7) : -1;
          win[k] = tex_pickWindow(J.day, J.lit, J.seed, cell, fl, J.litPct, J.night100, ds);
        }
        winLo = lo; winHi = hi;
      }
    }
    if (J.windows && !baseDone && T.base && yb > imax(ya, yE2)) {
      const uint16_t* w0 = tex_pickWindow(J.day, J.lit, J.seed, cell, 0, J.litPct, J.night100,
                                          J.dayState == -2 ? 0 : -1);
      palB[TS_WINA] = w0[0]; palB[TS_WINB] = w0[1]; palB[TS_WINC] = w0[2];
      baseDone = true;
    }
    // CAP
    if (T.cap && yE1 > ya)
      gfx_texColumn(x, ya, yE1, colp, (uint32_t)(fmaxF(0.0f, (ya + 0.5f - ty) * tppc) * sc), dv,
                    (uint32_t)((T.cap >> l) - 1), pal);
    // BODY (one call; window colours switch per floor inside the loop)
    if (yE2 > yB0) {
      const uint8_t* bodyM = colp + (T.cap >> l);
      const uint32_t v0 = (uint32_t)(fmaxF(0.0f, (yB0 + 0.5f - ty) * tppc - e1) * sc);
      const uint32_t mask = (uint32_t)((T.body >> l) - 1);
      if (nBody > 0) gfx_texColumnWin(x, yB0, yE2, bodyM, v0, dv, mask, bodyLog2 - l, pal, TS_WINA, win, nBody);
      else gfx_texColumn(x, yB0, yE2, bodyM, v0, dv, mask, pal);
    }
    // BASE
    const int yS0 = imax(ya, yE2);
    if (T.base && yb > yS0) {
      const uint8_t* baseM = colp + ((T.cap + T.body) >> l);
      const uint32_t v0 = (uint32_t)(fmaxF(0.0f, (yS0 + 0.5f - ty) * tppc - e2) * sc);
      gfx_texColumn(x, yS0, yb, baseM, v0, dv, (uint32_t)((T.base >> l) - 1), palB);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  PALETTES
//  Level 1, per frame: the unfogged palettes of a look (texture, colour
//  variant, face), built on first use (16 entries, 2-way).
//  Level 2, per slice: fogged copies, kept per side and face, so
//  consecutive slices of a building with the same fog reuse them.
// ═══════════════════════════════════════════════════════════════
struct PalSet {                            // fogged, ready to draw
  uint32_t key;
  uint16_t pal[TS_COUNT], base[TS_COUNT];
  uint16_t day[TEX_WIN_STATES][3];
  uint16_t lit[4][3];
  uint16_t farCol;
};
struct BasePal {                           // unfogged, per look
  uint32_t key;
  uint16_t pal[TS_COUNT], base[TS_COUNT];
  uint16_t day[TEX_WIN_STATES][3];
};
#define BASE_CACHE 8
static BasePal  sBaseCache[BASE_CACHE];   // 2-way: sets of 2 entries
static PalSet   sFogCache[4];             // [side L, side R, front L, front R]
static uint16_t sLit[4][3];               // night window lights (unfogged), per frame
static uint32_t sFrame = 1;

void buildings_beginFrame() {
  sFrame++;
  if ((sFrame & 0xFFFFu) == 0) sFrame++;
  tex_litSets(sLit, 0, 0);
}

static inline void fogRun(uint16_t* dst, const uint16_t* src, int n, uint32_t a) {
  if (a == 0) { memcpy(dst, src, (size_t)n * sizeof(uint16_t)); return; }
  for (int i = 0; i < n; i++) dst[i] = blend565(pal.fog, src[i], a);
}

static const PalSet& palettes(int slot, const Look& L, uint8_t style, int face, int fog32) {
  const uint32_t frame = sFrame & 0xFFFFu;
  const uint32_t look = (uint32_t)L.tex | ((uint32_t)L.colors << 4) | ((uint32_t)face << 6);
  const uint32_t bkey = look | (frame << 16) | 0x8000u;
  const uint32_t set = ((look * 0x9E3779B1u) >> 30) * 2;   // 0, 2, 4, 6
  BasePal* B = &sBaseCache[set];
  if (B->key != bkey) {
    BasePal* B2 = &sBaseCache[set + 1];
    if (B2->key == bkey) B = B2;
    else {
      // replace an entry of an older frame first
      if (((B->key >> 16) & 0xFFFFu) == frame) B = B2;
      B->key = bkey;
      const TexColors& C = tex_colors(L.tex, L.colors);
      tex_buildPalette(B->pal, C, face, 0, 0);
      const float up = pal.night * 0.55f;
      if (up > 0.02f) tex_buildUplitPalette(B->base, C, face, 0, 0, up);
      else memcpy(B->base, B->pal, sizeof(B->base));
      if (L.windows) tex_windowSets(B->day, face, C.rgb[TS_WINA], L.curtain, 0, 0);
      else memset(B->day, 0, sizeof(B->day));
    }
  }
  PalSet& P = sFogCache[slot & 3];
  const uint32_t key = (uint32_t)style | ((uint32_t)face << 8) | ((uint32_t)(fog32 + 1) << 10) | (frame << 16);
  if (P.key != key) {
    const uint32_t a = (uint32_t)(fog32 > 0 ? fog32 : 0);
    fogRun(P.pal, B->pal, TS_COUNT, a);
    fogRun(P.base, B->base, TS_COUNT, a);
    fogRun(&P.day[0][0], &B->day[0][0], TEX_WIN_STATES * 3, a);
    fogRun(&P.lit[0][0], &sLit[0][0], 4 * 3, a);
    // flat LOD colour: material with a share of (lit) window colour
    if (L.windows) {
      uint16_t win = lerpCol(P.day[0][0], P.lit[0][0], pal.night * L.litPct * 0.01f);
      P.farCol = lerpCol(P.pal[TS_BASE], win, 0.35f);
    } else {
      P.farCol = P.pal[TS_BASE];
    }
    P.key = key;
  }
  return P;
}

// ═══════════════════════════════════════════════════════════════
//  ROOFS
// ═══════════════════════════════════════════════════════════════
// Point on the roof plane of the slice: t 0 near .. 1 far, d 0 road side
// .. 1 back of the building, lifted by hPS (px * scale units)
struct RoofFrame { float sN, sF, oN, oF, tN, tF, scN, scF; };

static inline void roofPt(const RoofFrame& R, float t, float d, float hPS, float& x, float& y) {
  float xs = R.sN + (R.sF - R.sN) * t, xo = R.oN + (R.oF - R.oN) * t;
  x = xs + (xo - xs) * d;
  y = R.tN + (R.tF - R.tN) * t - (R.scN + (R.scF - R.scN) * t) * hPS;
}

static void flatRoof(const RoofFrame& R, const TexColors& C, int sIdx, bool isLeft, float fog,
                      bool showFront, float floorPS) {
  const int f32 = (int)(fog * 32.0f + 0.5f);
  uint16_t P[TS_COUNT];
  tex_buildPalette(P, C, TF_ROOF, pal.fog, f32);
  const uint16_t roofCol = lerpCol(P[TS_ALT2], P[TS_SHADE], 0.5f);
  gfx_fillQuad(R.sN, R.tN, R.sF, R.tF, R.oF, R.tF, R.oN, R.tN, roofCol);
  // parapet: lit coping along the road edge (and the front edge)
  float ax, ay, bx, by, cx, cy, dx, dy;
  roofPt(R, 0, 0, 0, ax, ay); roofPt(R, 1, 0, 0, bx, by);
  roofPt(R, 1, 0.05f, 0, cx, cy); roofPt(R, 0, 0.05f, 0, dx, dy);
  gfx_fillQuad(ax, ay, bx, by, cx, cy, dx, dy, P[TS_TRIM]);
  if (showFront) {
    roofPt(R, 0, 1, 0, cx, cy); roofPt(R, 0.12f, 1, 0, dx, dy);
    roofPt(R, 0.12f, 0, 0, bx, by);
    gfx_fillQuad(ax, ay, bx, by, dx, dy, cx, cy, P[TS_TRIM]);
  }
  // rooftop details: AC units, stair housing, water tank
  uint32_t h = hash32((uint32_t)sIdx * 0x2C1Bu + (isLeft ? 0x77u : 0x13u));
  if ((h & 3) == 0) return;
  float t0 = 0.15f + (float)((h >> 4) & 7) * 0.06f, d0 = 0.25f + (float)((h >> 8) & 7) * 0.06f;
  float tl = 0.25f + (float)((h >> 12) & 3) * 0.08f, dl = 0.12f + (float)((h >> 14) & 3) * 0.05f;
  float hb = floorPS * (0.35f + (float)((h >> 16) & 3) * 0.15f);
  float x0, y0, x1, y1, x2, y2, x3, y3, u0, v0, u1, v1;
  // front face (towards the camera) and top of a box
  roofPt(R, t0, d0, 0, x0, y0); roofPt(R, t0, d0 + dl, 0, x1, y1);
  roofPt(R, t0, d0 + dl, hb, x2, y2); roofPt(R, t0, d0, hb, x3, y3);
  gfx_fillQuad(x0, y0, x1, y1, x2, y2, x3, y3, P[TS_SHADE]);
  roofPt(R, t0 + tl, d0, hb, u0, v0); roofPt(R, t0 + tl, d0 + dl, hb, u1, v1);
  gfx_fillQuad(x3, y3, x2, y2, u1, v1, u0, v0, P[(h >> 20) & 1 ? TS_LIGHT : TS_TRIM]);
  // side face towards the road
  roofPt(R, t0 + tl, d0, 0, u1, v1);
  gfx_fillQuad(x0, y0, x3, y3, u0, v0, u1, v1, P[TS_BASE]);
}

static void pitchedRoof(const RoofFrame& R, const Look& L, uint8_t style, float fog, bool showFront,
                        float ridgePS, const uint16_t* frontPal) {
  const int f32 = (int)(fog * 32.0f + 0.5f);
  uint16_t P[TS_COUNT];
  tex_buildPalette(P, tex_colors(WT_ROOF, (bldVariant(style) >> 2) & 3), TF_ROOF, pal.fog, f32);
  // road-facing plane: courses of tiles from the eave to the ridge
  const int N = 5;
  for (int k = 0; k < N; k++) {
    float a = (float)k / N, b = (float)(k + 1) / N;
    float ax, ay, bx, by, cx, cy, dx, dy;
    roofPt(R, 0, a * 0.5f, ridgePS * a, ax, ay); roofPt(R, 1, a * 0.5f, ridgePS * a, bx, by);
    roofPt(R, 1, b * 0.5f, ridgePS * b, cx, cy); roofPt(R, 0, b * 0.5f, ridgePS * b, dx, dy);
    gfx_fillQuad(ax, ay, bx, by, cx, cy, dx, dy, (k & 1) ? P[TS_BASE] : P[TS_LIGHT]);
    // overlap shadow at the lower edge of each course
    float m = a + (b - a) * 0.22f, ex, ey, fx, fy;
    roofPt(R, 1, m * 0.5f, ridgePS * m, ex, ey); roofPt(R, 0, m * 0.5f, ridgePS * m, fx, fy);
    gfx_fillQuad(ax, ay, bx, by, ex, ey, fx, fy, P[TS_SHADE]);
  }
  // ridge tiles
  float ax, ay, bx, by, cx, cy, dx, dy;
  roofPt(R, 0, 0.47f, ridgePS * 0.94f, ax, ay); roofPt(R, 1, 0.47f, ridgePS * 0.94f, bx, by);
  roofPt(R, 1, 0.5f, ridgePS * 1.04f, cx, cy); roofPt(R, 0, 0.5f, ridgePS * 1.04f, dx, dy);
  gfx_fillQuad(ax, ay, bx, by, cx, cy, dx, dy, P[TS_DARK]);
  // gable on the front facade, with a tiled barge edge
  if (showFront) {
    float gx0, gy0, gx1, gy1, gx2, gy2;
    roofPt(R, 0, 0, 0, gx0, gy0);
    roofPt(R, 0, 1, 0, gx1, gy1);
    roofPt(R, 0, 0.5f, ridgePS, gx2, gy2);
    gfx_fillTri(gx0, gy0, gx1, gy1, gx2, gy2, frontPal[TS_BASE]);
    float e = fmaxF(1.0f, (gy0 - gy2) * 0.10f);
    gfx_fillQuad(gx0, gy0, gx2, gy2, gx2, gy2 + e, gx0, gy0 + e, P[TS_SHADE]);
    gfx_fillQuad(gx1, gy1, gx2, gy2, gx2, gy2 + e, gx1, gy1 + e, P[TS_SHADE]);
  }
  (void)L;
}

// ═══════════════════════════════════════════════════════════════
//  MAIN ENTRY
// ═══════════════════════════════════════════════════════════════
void drawBuilding(const ProjPt& pn, const ProjPt& pf, int heightUnits, uint8_t style,
                  int sIdx, bool isLeft, bool showFront, float fog) {
  if (heightUnits <= 0) return;
  if (!textures_ready()) textures_init();
  const Look L = lookOf(style);
  const WallTex& T = tex_wall(L.tex);
  const float dir = isLeft ? -1.0f : 1.0f;
  const float Hu = (float)heightUnits * BUILDING_H_UNIT;          // shared units
  const float Hs = Hu * BLD_V_SCALE;                               // px * scale
  const float floorPS = BUILDING_FLOOR_H * BLD_V_SCALE;
  const float depthPS = BUILDING_W * BLD_DEPTH_SCALE;
  const float ridgePS = L.roof == ROOF_PITCHED ? BLD_RIDGE_FLOORS * floorPS : 0.0f;

  const float sideN = pn.x + dir * pn.w * BUILDING_OFFSET;
  const float sideF = pf.x + dir * pf.w * BUILDING_OFFSET;
  const float outN = sideN + dir * pn.scale * depthPS;
  const float outF = sideF + dir * pf.scale * depthPS;
  const float gN = pn.y, gF = pf.y;
  const float topN = gN - pn.scale * Hs, topF = gF - pf.scale * Hs;

  // Reject slices entirely off screen / under nearer road
  const float minX = fminF(fminF(sideN, sideF), fminF(outN, outF));
  const float maxX = fmaxF(fmaxF(sideN, sideF), fmaxF(outN, outF));
  if (maxX < gfx_cx0 || minX >= gfx_cx1) return;
  if (fminF(topN - pn.scale * ridgePS, topF - pf.scale * ridgePS) >= gfx_cy1) return;

  const int fog32 = (int)(fog * 32.0f + 0.5f);
  const int sideFace = (isLeft == (pal.sunDirX >= 0)) ? TF_LIT : TF_SHADE;
  const PalSet& SP = palettes(isLeft ? 0 : 1, L, style, sideFace, fog32);

  // Vertical layout in texels
  const float vTot = Hu / BUILDING_FLOOR_H * TEX_V_PER_FLOOR;
  int floors = 0;
  float capV, baseV;
  if (L.floored) {
    floors = (int)(Hu / BUILDING_FLOOR_H);
    if (floors < 1) { floors = 0; capV = vTot; baseV = 0; }
    else { capV = vTot - floors * (float)TEX_V_PER_FLOOR; baseV = T.base ? (float)TEX_V_PER_FLOOR : 0.0f; }
  } else {
    capV = fminF((float)T.cap, vTot);
    baseV = T.base ? fminF((float)T.base, fmaxF(0.0f, vTot - capV)) : 0.0f;
  }

  WallJob J;
  J.tex = &T;
  J.vTot = vTot;
  J.capV = capV;
  J.baseV = baseV;
  J.floors = floors;
  J.dayState = L.curtain ? -2 : -1;
  J.windows = L.windows && floors > 0;
  J.jag = L.jagged ? 7.0f : 0.0f;
  J.seed = hash32((uint32_t)style * 0x45D9F3Bu + (isLeft ? 0x1234u : 0x4321u));
  J.litPct = L.litPct;
  J.night100 = (int)(pal.night * 100.0f);
  J.day = SP.day;
  J.lit = SP.lit;

  // 1. SIDE WALL facing the road: u along the road (perspective-correct)
  {
    const float nearZ = pf.z - SEG_LEN;                     // unclipped near boundary
    const float frac = clampF((pn.z - nearZ) / SEG_LEN, 0.0f, 1.0f);
    const float uN = ((float)sIdx + frac) * TEX_U_PER_SEG, uF = ((float)sIdx + 1.0f) * TEX_U_PER_SEG;
    J.xa = sideN; J.xb = sideF;
    J.gya = gN; J.gyb = gF;
    J.tya = topN; J.tyb = topF;
    J.iza = 1.0f / pn.z; J.izb = 1.0f / pf.z;
    J.uza = uN * J.iza; J.uzb = uF * J.izb;
    J.pal = SP.pal;
    J.palBase = SP.base;
    J.farCol = SP.farCol;
    drawWallColumns(J);
  }

  // 2. ROOF (only when below the eye)
  RoofFrame RF = { sideN, sideF, outN, outF, topN, topF, pn.scale, pf.scale };
  if (L.roof == ROOF_PITCHED) {
    const PalSet& FP = palettes(isLeft ? 2 : 3, L, style, TF_FRONT, fog32);
    pitchedRoof(RF, L, style, fog, showFront, ridgePS, FP.pal);
  } else if (topN > topF + 0.5f) {
    if (L.roof == ROOF_FLAT) {
      flatRoof(RF, tex_colors(L.tex, L.colors), sIdx, isLeft, fog, showFront, floorPS);
    } else {   // natural top: scrub on rock, coping on walls
      const uint16_t* P = SP.pal;
      gfx_fillQuad(sideN, topN, sideF, topF, outF, topF, outN, topN,
                   L.jagged ? P[TS_ACC2] : P[TS_TRIM]);
    }
  }

  // 3. FRONT FACADE (first slice of the building): constant depth, affine u
  if (showFront) {
    const PalSet& FP = palettes(isLeft ? 2 : 3, L, style, TF_FRONT, fog32);
    const float uS = (float)sIdx * TEX_U_PER_SEG + 7.0f;
    const float uW = depthPS / SCR_CX * ((float)TEX_U_PER_SEG / SEG_LEN);
    WallJob F = J;
    F.xa = sideN; F.xb = outN;
    F.gya = F.gyb = gN;
    F.tya = F.tyb = topN;
    F.iza = F.izb = 1.0f / pn.z;
    F.uza = uS * F.iza; F.uzb = (uS + uW) * F.izb;
    F.pal = FP.pal;
    F.palBase = FP.base;
    F.day = FP.day;
    F.lit = FP.lit;
    F.farCol = FP.farCol;
    drawWallColumns(F);
  }

  // 4. NIGHT GLOW: tower crowns and shop signs
  if (L.neon && pal.night > 0.3f) {
    const TexColors& C = tex_colors(L.tex, L.colors);
    const uint16_t glow = rgb(C.glow[1][0], C.glow[1][1], C.glow[1][2]);
    const int a = (int)(10.0f * (pal.night - 0.3f) / 0.7f * (1.0f - fog));
    if (a > 0) {
      const float ppt = pn.scale * floorPS / TEX_V_PER_FLOOR, pptF = pf.scale * floorPS / TEX_V_PER_FLOOR;
      if (bldType(style) == BLD_SHOPS && floors >= 1) {
        // halo over the sign board (rows 2..6 of the ground floor)
        float sN0 = gN - 15.0f * ppt, sN1 = gN - 9.0f * ppt;
        float sF0 = gF - 15.0f * pptF, sF1 = gF - 9.0f * pptF;
        gfx_fillQuad(sideN, sN0, sideF, sF0, sideF, sF1, sideN, sN1, glow, GFX_ADD, a);
      } else if (bldType(style) == BLD_MODERN) {
        // crown halo above the roof line
        float hN = fmaxF(1.0f, 6.0f * ppt), hF = fmaxF(1.0f, 6.0f * pptF);
        gfx_fillQuad(sideN, topN, sideF, topF, sideF, topF - hF, sideN, topN - hN, glow, GFX_ADD, a);
      }
    }
  }
}
