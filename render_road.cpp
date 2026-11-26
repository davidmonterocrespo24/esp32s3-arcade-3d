/*
  ═══════════════════════════════════════════════════════════════
  ROAD PASS — implementation

  Pass 1 (front to back): project every segment boundary once and
  record, per segment, the screen row below which nearer road already
  covers everything (clipRow).
  Pass 2 (back to front): for each segment draw road rows, buildings,
  tunnel, rails, scenery, lamps, cars and the tunnel portal, clipped to
  clipRow, so hills hide what is behind them and nearer things paint
  over farther ones (painter's algorithm).

  Road rows are built from per-segment region specs: boundaries at fixed
  fractions of the road half-width (ground materials, kerbs, rumble,
  edge lines, lane dashes, zebra crossings, the start line), coloured
  per segment, fogged per row from the true depth, anti-aliased by
  gfx_rowRegions.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include "render_road.h"
#include "render_building.h"
#include "render_scenery.h"
#include "render_cars.h"
#include "gfx.h"
#include "colors.h"
#include "track.h"
#include "utils.h"
#include <math.h>

RoadCam roadCam = { 0, CAM_HEIGHT, 1 };
float roadBX[DRAW_DIST + 1], roadBY[DRAW_DIST + 1], roadBZ[DRAW_DIST + 1];
int   roadBaseIdx = 0;
float roadPosOff = 0;

static const float NEAR_Z = ROAD_NEAR_Z;
static const float WALL_K = TUNNEL_WALL_X;   // tunnel wall position (road half-widths)
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

static inline void project(ProjPt& p, float wx, float wy, float wz) { roadProject(p, wx, wy, wz); }

// ═══════════════════════════════════════════════════════════════
//  ROAD ROWS
// ═══════════════════════════════════════════════════════════════
#define MAXB 44                  // region boundaries per row
struct BandSpec {
  int n;                         // boundaries; n + 1 regions
  float u[MAXB];                 // boundary offsets in road half-widths (ascending)
  uint16_t c[MAXB + 1];          // region colours before fog
};

static inline void addB(BandSpec& S, float u, uint16_t after) {
  if (S.n < MAXB) { S.u[S.n++] = u; S.c[S.n] = after; }
}

static const float RW = 0.11f;   // rumble width
static const float EW = 0.035f;  // edge line width
static const float LW = 0.03f;   // lane line width

// What lies beyond one road edge, from the edge outwards: region i spans
// up to u[i] (road half-widths from the centre); the last one is unbounded.
struct SideProfile { int n; float u[4]; uint16_t c[4]; };

static void sideProfile(SideProfile& P, const Segment& seg, int side, bool light) {
  const int k = light ? 0 : 1;
  P.n = 0;
  if (seg.isTunnel()) {
    P.u[0] = 1.0f + RW; P.c[0] = light ? pal.tunnelWallD : pal.tunnelCeil;
    P.u[1] = 99.0f;     P.c[1] = light ? pal.tunnelWallL : pal.tunnelWallD;
    P.n = 2;
    return;
  }
  const int mat = seg.mat(side);
  const uint16_t rumble = light ? pal.rumbleL : pal.rumbleD;
  const uint16_t ground = pal.ground[mat < MAT_COUNT ? mat : 0][k];
  switch (mat) {
    case MAT_SIDEWALK:   // kerb, sidewalk up to the building line, side street beyond
      P.u[0] = SIDEWALK_KERB_X; P.c[0] = light ? pal.kerbL : pal.kerbD;
      P.u[1] = BUILDING_OFFSET; P.c[1] = ground;
      P.u[2] = 99.0f; P.c[2] = darkenCol(pal.roadD, 0.82f);
      P.n = 3;
      break;
    case MAT_SAND:       // beach, then the sea
      P.u[0] = 1.0f + RW; P.c[0] = rumble;
      P.u[1] = BEACH_WATER_X; P.c[1] = ground;
      P.u[2] = 99.0f;     P.c[2] = pal.ground[MAT_WATER][k];
      P.n = 3;
      break;
    case MAT_WATER:      // narrow sea-wall top, then water
      P.u[0] = 1.0f + RW; P.c[0] = rumble;
      P.u[1] = SEAWALL_X; P.c[1] = pal.ground[MAT_SIDEWALK][0];
      P.u[2] = 99.0f;     P.c[2] = ground;
      P.n = 3;
      break;
    default:
      P.u[0] = 1.0f + RW; P.c[0] = rumble;
      P.u[1] = 99.0f;     P.c[1] = ground;
      P.n = 2;
      break;
  }
}

static void buildSpec(BandSpec& S, const Segment& seg, int sIdx, int checkerPhase) {
  const bool light = ((sIdx / RUMBLE_LEN) & 1) == 0;
  const bool tun = seg.isTunnel();
  uint16_t road = light ? pal.roadL : pal.roadD, edge = pal.edgeLine, lane = pal.lane;
  if (tun) {
    road = darkenCol(road, 0.72f);
    edge = darkenCol(edge, 0.7f);
    lane = darkenCol(lane, 0.75f);
  }
  SideProfile L, R;
  sideProfile(L, seg, 0, light);
  sideProfile(R, seg, 1, light);

  // Left side: outermost region first, then towards the road edge
  S.n = 0;
  S.c[0] = L.c[L.n - 1];
  for (int i = L.n - 2; i >= 0; i--) addB(S, -L.u[i], L.c[i]);
  addB(S, -1.0f, edge);

  if (seg.flags & SEGF_START) {
    // chequered start/finish line across the road (two rows: checkerPhase)
    const int CH = 16;
    float w = (2.0f - 2.0f * EW) / CH;
    for (int i = 0; i < CH; i++)
      addB(S, -1.0f + EW + w * i, ((i + checkerPhase) & 1) ? COL_WHITE : road);
  } else {
    addB(S, -1.0f + EW, road);
    if (seg.flags & SEGF_CROSSWALK) {
      // zebra bars parallel to the driving direction
      const int BARS = 8;
      const float span = 1.76f, bw = 0.11f, step = (span - bw) / (BARS - 1);
      for (int i = 0; i < BARS; i++) {
        float a = -0.88f + step * i;
        addB(S, a, lane);
        addB(S, a + bw, road);
      }
    } else if (light) {
      for (int l = 1; l < LANES; l++) {
        float lx = -1.0f + 2.0f * l / LANES;
        addB(S, lx - LW * 0.5f, lane);
        addB(S, lx + LW * 0.5f, road);
      }
    }
  }
  addB(S, 1.0f - EW, edge);

  // Right side: from the road edge outwards
  addB(S, 1.0f, R.c[0]);
  for (int i = 1; i < R.n; i++) addB(S, R.u[i - 1], R.c[i]);
}

static void drawBand(const SegView& s, const Segment& seg, int sIdx) {
  int r0 = rowOf(s.f.y), r1 = imin(rowOf(s.n.y), s.clipRow);
  if (r1 <= r0) return;
  float dy = s.n.y - s.f.y;
  if (dy < 1e-3f) return;

  const bool tun = seg.isTunnel();
  const bool start = (seg.flags & SEGF_START) != 0;
  static BandSpec spec[2];
  buildSpec(spec[0], seg, sIdx, 0);
  if (start) buildSpec(spec[1], seg, sIdx, 1);
  const uint16_t fogc = tun ? COL_BLACK : pal.fog;

  float invZf = 1.0f / s.f.z, invZn = 1.0f / s.n.z;
  int lastA = -1, lastSpec = -1;
  float b[MAXB];
  uint16_t c[MAXB + 1];

  for (int y = r0; y < r1; y++) {
    float t = (y + 0.5f - s.f.y) / dy;              // 0 far .. 1 near
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float cx = s.f.x + (s.n.x - s.f.x) * t;
    float hw = s.f.w + (s.n.w - s.f.w) * t;
    float z = 1.0f / (invZf + (invZn - invZf) * t);
    int a = fogAlpha(tun ? z * 1.6f : z);
    if (tun && a > 28) a = 28;
    int si = (start && t >= 0.5f) ? 1 : 0;
    const BandSpec& S = spec[si];
    if (a != lastA || si != lastSpec) {
      for (int k = 0; k <= S.n; k++) c[k] = fogMix(S.c[k], fogc, a);
      lastA = a;
      lastSpec = si;
    }
    for (int k = 0; k < S.n; k++) b[k] = cx + S.u[k] * hw;
    gfx_rowRegions(y, b, c, S.n + 1);
  }
}

// ═══════════════════════════════════════════════════════════════
//  TUNNEL
// ═══════════════════════════════════════════════════════════════
static inline float ceilY(const ProjPt& p, float worldY) {
  return SCR_CY - p.scale * (worldY + TUNNEL_H - roadCam.camY) * SCR_CY;
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
  float topY = SCR_CY - sc * (s.nyW + TUNNEL_H * 2.1f - roadCam.camY) * SCR_CY;
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
//  ROADSIDE: lamps, guard rails, start gantry
// ═══════════════════════════════════════════════════════════════
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

// Guard rail along one road edge over the whole segment (beam + posts)
static void drawRail(const SegView& s, int side, int sIdx) {
  const float H0 = 130.0f, H1 = 290.0f;      // beam bottom / top (world units)
  float xN = s.n.x + side * s.n.w * RAIL_X, xF = s.f.x + side * s.f.w * RAIL_X;
  float bN = s.n.y - s.n.scale * H0 * SCR_CY, tN = s.n.y - s.n.scale * H1 * SCR_CY;
  float bF = s.f.y - s.f.scale * H0 * SCR_CY, tF = s.f.y - s.f.scale * H1 * SCR_CY;
  if (fmaxF(xN, xF) < -4 || fminF(xN, xF) > SCR_W + 4) return;
  uint16_t beam = fogF(lerpCol(rgb(196, 200, 206), pal.fog, 0.15f), s.fog);
  uint16_t dark = darkenCol(beam, 0.55f);
  gfx_fillQuad(xN, bN, xF, bF, xF, tF, xN, tN, beam);
  // a darker groove along the middle of the beam
  float mN = (bN + tN) * 0.5f, mF = (bF + tF) * 0.5f;
  float gN = fmaxF(0.5f, (bN - tN) * 0.12f), gF = fmaxF(0.5f, (bF - tF) * 0.12f);
  gfx_fillQuad(xN, mN - gN, xF, mF - gF, xF, mF + gF, xN, mN + gN, dark);
  if ((sIdx & 1) == 0) {   // post at the near end every other segment
    float pw = fmaxF(0.6f, s.n.scale * 50.0f * SCR_CX);
    gfx_fillQuad(xN - pw, s.n.y, xN + pw, s.n.y, xN + pw, tN, xN - pw, tN, dark);
  }
}

// Start/finish gantry over the line: two posts and a chequered banner
static void drawGantry(const ProjPt& p, float fog) {
  const float H0 = 2300.0f, H1 = 2900.0f, X = 1.32f;
  float sc = p.scale;
  if (sc * H1 * SCR_CY < 4) return;
  float lx = p.x - p.w * X, rx = p.x + p.w * X;
  float yb = p.y - sc * H0 * SCR_CY, yt = p.y - sc * H1 * SCR_CY;
  float pw = fmaxF(1.0f, sc * 110.0f * SCR_CX);
  uint16_t post = fogF(rgb(70, 72, 80), fog);
  gfx_fillQuad(lx - pw, p.y, lx + pw, p.y, lx + pw, yt, lx - pw, yt, post);
  gfx_fillQuad(rx - pw, p.y, rx + pw, p.y, rx + pw, yt, rx - pw, yt, post);
  // banner: two rows of checks
  const int CH = 14;
  float w = (rx - lx) / CH, ym = (yb + yt) * 0.5f;
  uint16_t white = fogF(COL_WHITE, fog), black = fogF(rgb(20, 20, 24), fog);
  for (int i = 0; i < CH; i++) {
    float x0 = lx + w * i, x1 = x0 + w;
    gfx_fillQuad(x0, yt, x1, yt, x1, ym, x0, ym, (i & 1) ? white : black);
    gfx_fillQuad(x0, ym, x1, ym, x1, yb, x0, yb, (i & 1) ? black : white);
  }
  if (pal.night > 0.3f)
    gfx_fillQuad(lx, yb, rx, yb, rx, yb + fmaxF(1, (yb - yt) * 0.12f), lx, yb + fmaxF(1, (yb - yt) * 0.12f),
                 pal.lampLight, GFX_ADD, (int)(20 * pal.night * (1 - fog)));
}

// ═══════════════════════════════════════════════════════════════
//  MAIN PASS
// ═══════════════════════════════════════════════════════════════
void drawRoad(float position, float playerX, float playerZdist, float cameraDepth) {
  if (!sFogInit) initFog();
  if (segCount <= 0) return;
  int baseIdx = findSegIdx(position);
  float posOff = position - floorf(position / SEG_LEN) * SEG_LEN;
  float basePct = posOff / SEG_LEN;
  roadBaseIdx = baseIdx;
  roadPosOff = posOff;

  int pSeg = findSegIdx(position + playerZdist);
  float pPct = percentRemaining(position + playerZdist, SEG_LEN);
  float playerY = lerpF(segAt(pSeg - 1).y, segAt(pSeg).y, pPct);
  roadCam.camY = playerY + CAM_HEIGHT;
  roadCam.camX = playerX * ROAD_W;
  roadCam.camDepth = cameraDepth;

  // World data of every boundary: centre offset (curve accumulation),
  // elevation (far end of the previous segment) and camera-space z.
  {
    float x = 0, dx = -(segAt(baseIdx).curve * basePct);
    for (int n = 0; n <= DRAW_DIST; n++) {
      roadBX[n] = x;
      roadBY[n] = segAt(baseIdx + n - 1).y;
      roadBZ[n] = n * SEG_LEN - posOff;
      if (n < DRAW_DIST) { x += dx; dx += segAt(baseIdx + n).curve; }
    }
  }

  // ---- Pass 1: front to back ----
  float maxy = SCR_H;
  int lastValid = -1;
  for (int n = 0; n < DRAW_DIST; n++) {
    SegView& s = sv[n];
    s.clipRow = rowOf(maxy);
    s.roadVis = false;
    s.valid = roadBZ[n + 1] > NEAR_Z;
    if (!s.valid) continue;
    lastValid = n;
    float nz = roadBZ[n], nx = roadBX[n], ny = roadBY[n];
    if (nz < NEAR_Z) {    // clip the near end to the near plane
      float t = (NEAR_Z - nz) / (roadBZ[n + 1] - nz);
      nx += (roadBX[n + 1] - roadBX[n]) * t;
      ny += (roadBY[n + 1] - roadBY[n]) * t;
      nz = NEAR_Z;
    }
    project(s.n, nx, ny, nz);
    project(s.f, roadBX[n + 1], roadBY[n + 1], roadBZ[n + 1]);
    s.nyW = ny; s.fyW = roadBY[n + 1];
    s.nxW = nx; s.fxW = roadBX[n + 1];
    s.fog = sFogLUT[iclamp((int)((nz + roadBZ[n + 1]) * 0.5f * (FOG_LUT / MAX_Z)), 0, FOG_LUT)] / 32.0f;
    float top = s.f.y, bot = fminF(s.n.y, maxy);
    if (top < bot) { s.roadVis = true; maxy = top; }
  }

  // Ground beyond the draw distance (below the horizon, above the road)
  int horizonRow = SCR_CY;
  int groundEnd = rowOf(maxy);
  for (int y = horizonRow; y < groundEnd; y++) gfx_hspan(y, 0, SCR_W, pal.fog);

  cars_prepare(position);

  // ---- Pass 2: back to front ----
  for (int n = DRAW_DIST - 1; n >= 0; n--) {
    SegView& s = sv[n];
    if (!s.valid) continue;
    int sIdx = segWrap(baseIdx + n);
    const Segment& seg = segments[sIdx];
    const Segment& prevSeg = segAt(sIdx - 1);
    const bool tunnel = seg.isTunnel();
    gfx_setClip(0, 0, SCR_W, s.clipRow);

    if (s.roadVis) drawBand(s, seg, sIdx);

    if (tunnel) {
      drawTunnelSeg(s, sIdx, n == lastValid);
    } else {
      for (int side = 0; side < 2; side++) {
        if (!seg.build[side]) continue;
        bool front = prevSeg.build[side] != seg.build[side] || prevSeg.style[side] != seg.style[side] ||
                     prevSeg.isTunnel();
        drawBuilding(s.n, s.f, seg.build[side], seg.style[side], sIdx, side == 0, front, s.fog);
      }
      if (seg.flags & SEGF_RAIL_L) drawRail(s, -1, sIdx);
      if (seg.flags & SEGF_RAIL_R) drawRail(s, +1, sIdx);
    }

    if (n >= 1 && s.n.z > NEAR_Z) {
      for (int side = 0; side < 2; side++) {
        if (seg.spr[side] < 0) continue;
        float sx = s.n.x + s.n.scale * seg.sprX(side) * ROAD_W * SCR_CX;
        drawSceneSprite(seg.spr[side], sx, s.n.y, s.n.scale, s.fog);
      }
      if (seg.flags & SEGF_LAMP_L) drawLamp(s.n, -1, s.fog);
      if (seg.flags & SEGF_LAMP_R) drawLamp(s.n, +1, s.fog);
      if (seg.flags & SEGF_START) drawGantry(s.n, s.fog);
    }

    cars_drawSlice(n, s.fog, tunnel);

    if (tunnel && !prevSeg.isTunnel() && n >= 1) drawPortal(s);
  }
  gfx_resetClip();
}
