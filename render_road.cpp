/*
  ═══════════════════════════════════════════════════════════════
  ROAD PASS — implementation

  Pass 1 (front to back): project every segment boundary once and
  record, per segment, the screen row below which nearer road already
  covers everything (clipRow).
  Pass 2 (back to front): for each segment draw road rows, buildings,
  tunnel, rails, scenery, lamps, gantry, cars and the tunnel portal,
  clipped to clipRow, so hills hide what is behind them and nearer
  things paint over farther ones (painter's algorithm).

  Road rows are built from per-segment region specs: boundaries at fixed
  fractions of the road half-width (ground materials, kerbs, rumble,
  edge lines, lane dashes, zebra crossings, stop lines, the start
  line), coloured per segment, fogged per row from the true depth,
  anti-aliased by gfx_rowRegionsTex. Near rows sample procedural
  textures (asphalt, pavement + kerb, grass, sand, water, rock, snow,
  dirt) through a per-row mip level, so they fade into the flat bands
  with distance instead of shimmering.
  ═══════════════════════════════════════════════════════════════
*/

#pragma GCC optimize("O2")

#include <Arduino.h>          // millis(): water animation
#include "render_road.h"
#include "render_building.h"
#include "render_scenery.h"
#include "render_cars.h"
#include "textures.h"
#include "gfx.h"
#include "colors.h"
#include "track.h"
#include "circuits.h"
#include "rendering.h"        // skyOffset: the sun glitter column follows the sun
#include "race.h"
#include "game.h"
#include "ui.h"               // start-light state of the countdown (gantry)
#include "utils.h"
#include <math.h>
#include <string.h>

RoadCam roadCam = { 0, CAM_HEIGHT, 1 };
float roadBX[DRAW_DIST + 1], roadBY[DRAW_DIST + 1], roadBZ[DRAW_DIST + 1];
int   roadBaseIdx = 0;
float roadPosOff = 0;

static const float NEAR_Z = ROAD_NEAR_Z;
static const float WALL_K = TUNNEL_WALL_X;   // tunnel wall position (road half-widths)
static const float MAX_Z  = (float)DRAW_DIST * SEG_LEN;
static const float TEX_MAX_Z = 32.0f * SEG_LEN;   // rows nearer than this may be textured
static const float SEA_FOG = 0.62f;               // the sea keeps its colour further out

struct SegView {
  ProjPt n, f;          // near (clipped) and far boundary projections
  float  nyW, fyW;      // world elevation at the near / far boundary
  float  nxW, fxW;      // world lateral offset of the road centre
  float  fog;           // 0..1 at mid-segment
  int    clipRow;       // rows >= clipRow are covered by nearer road
  bool   valid, roadVis;
};
static SegView sv[DRAW_DIST];

// Per-frame state
static float sTime = 0;          // seconds (water animation)
static int   sSunX = 0;          // glitter column
static float sGlint = 0;         // glitter strength 0..1 (0 = none)
static uint16_t sGlintCol = 0;
static uint16_t sSea = 0;        // sea colour: water reflecting the sky near the horizon

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

// the same pixel-centre rule for screen columns
static inline int colOf(float x) {
  if (x < -1.0f) return 0;
  if (x > SCR_W + 1.0f) return SCR_W;
  return iclamp((int)ceilf(x - 0.5f), 0, SCR_W);
}

static inline void project(ProjPt& p, float wx, float wy, float wz) { roadProject(p, wx, wy, wz); }

// ═══════════════════════════════════════════════════════════════
//  ROAD ROWS: region specs
// ═══════════════════════════════════════════════════════════════
// Texture source of a region (0 = flat colour)
enum TexKind : uint8_t {
  TK_NONE = 0, TK_ROAD, TK_PAVE_L, TK_PAVE_R, TK_GND_L, TK_GND_R, TK_WATER_L, TK_WATER_R,
  TK_STREET_L, TK_STREET_R, TK_GLINT, TK_COUNT };

#define MAXB 32                  // region boundaries per row (the widest spec uses 30)
struct BandSpec {
  int n;                         // boundaries; n + 1 regions
  float u[MAXB];                 // boundary offsets in road half-widths (ascending)
  uint16_t c[MAXB + 1];          // region colours before fog
  uint8_t  k[MAXB + 1];          // region texture kind
  bool     sea[MAXB + 1];        // water-like region (lighter fog)
};

static inline void addB(BandSpec& S, float u, uint16_t after, uint8_t kind = TK_NONE, bool sea = false) {
  if (S.n < MAXB) { S.u[S.n++] = u; S.c[S.n] = after; S.k[S.n] = kind; S.sea[S.n] = sea; }
}

static const float RW = 0.11f;   // rumble width
static const float EW = 0.035f;  // edge line width
static const float LW = 0.03f;   // lane line width

// What lies beyond one road edge, from the edge outwards: region i spans
// up to u[i] (road half-widths from the centre); the last one is unbounded.
struct SideProfile { int n; float u[6]; uint16_t c[6]; uint8_t k[6]; bool sea[6]; };

static inline void prof(SideProfile& P, float u, uint16_t c, uint8_t k = TK_NONE, bool sea = false) {
  if (P.n < 6) { P.u[P.n] = u; P.c[P.n] = c; P.k[P.n] = k; P.sea[P.n] = sea; P.n++; }
}

static void sideProfile(SideProfile& P, const Segment& seg, int sIdx, int side, bool light) {
  const int k = light ? 0 : 1;
  P.n = 0;
  if (seg.isTunnel()) {
    prof(P, 1.0f + RW, light ? pal.tunnelWallD : pal.tunnelCeil);
    prof(P, 99.0f, light ? pal.tunnelWallL : pal.tunnelWallD);
    return;
  }
  const uint8_t kPave = side ? TK_PAVE_R : TK_PAVE_L, kGnd = side ? TK_GND_R : TK_GND_L;
  const uint8_t kWater = side ? TK_WATER_R : TK_WATER_L, kStreet = side ? TK_STREET_R : TK_STREET_L;
  const int mat = seg.mat(side);
  const uint16_t rumble = light ? pal.rumbleL : pal.rumbleD;
  const uint16_t ground = pal.ground[mat < MAT_COUNT ? mat : 0][k];
  // waves washing up the beach / against the sea wall (rolls along the shore)
  const float wash = 0.05f * sinf(sTime * 1.6f + (float)sIdx * 0.21f);
  switch (mat) {
    case MAT_SIDEWALK:   // kerb, sidewalk slabs up to the building line, side street beyond
      prof(P, SIDEWALK_KERB_X, light ? pal.kerbL : lerpCol(pal.kerbL, pal.kerbD, 0.35f), kPave);
      prof(P, BUILDING_OFFSET, ground, kPave);
      prof(P, 99.0f, darkenCol(light ? pal.roadL : pal.roadD, 0.86f), seg.build[side] ? TK_NONE : kStreet);
      break;
    case MAT_SAND: {     // beach, wet sand, foam, the sea
      const uint16_t sand = pal.ground[MAT_SAND][0];
      prof(P, 1.0f + RW, rumble);
      prof(P, BEACH_WATER_X - 0.22f + wash, sand, kGnd);
      prof(P, BEACH_WATER_X + wash, lerpCol(sand, sSea, 0.30f));
      prof(P, BEACH_WATER_X + 0.06f + wash * 1.3f, lerpCol(sSea, COL_WHITE, 0.62f), TK_NONE, true);
      prof(P, 99.0f, sSea, kWater, true);
      break;
    }
    case MAT_WATER:      // narrow sea-wall top, foam at its foot, then water
      prof(P, 1.0f + RW, rumble);
      prof(P, SEAWALL_X, pal.ground[MAT_SIDEWALK][0]);
      prof(P, SEAWALL_X + 0.03f, darkenCol(pal.ground[MAT_SIDEWALK][1], 0.55f));
      prof(P, SEAWALL_X + 0.07f + wash * 0.5f, lerpCol(sSea, COL_WHITE, 0.55f), TK_NONE, true);
      prof(P, 99.0f, sSea, kWater, true);
      break;
    default:
      prof(P, 1.0f + RW, rumble);
      prof(P, 99.0f, ground, kGnd);
      break;
  }
}

// Spec variants of a segment's rows
enum { SPEC_NORMAL = 0, SPEC_CHECK_A, SPEC_CHECK_B, SPEC_STOP, SPEC_COUNT };

static void buildSpec(BandSpec& S, const Segment& seg, int sIdx, int variant) {
  const bool light = ((sIdx / RUMBLE_LEN) & 1) == 0;
  const bool tun = seg.isTunnel();
  uint16_t road = light ? pal.roadL : pal.roadD, edge = pal.edgeLine, lane = pal.lane;
  if (tun) {
    road = darkenCol(road, 0.72f);
    edge = darkenCol(edge, 0.7f);
    lane = darkenCol(lane, 0.75f);
  }
  SideProfile L, R;
  sideProfile(L, seg, sIdx, 0, light);
  sideProfile(R, seg, sIdx, 1, light);

  // Left side: outermost region first, then towards the road edge
  S.n = 0;
  S.c[0] = L.c[L.n - 1];
  S.k[0] = L.k[L.n - 1];
  S.sea[0] = L.sea[L.n - 1];
  for (int i = L.n - 2; i >= 0; i--) addB(S, -L.u[i], L.c[i], L.k[i], L.sea[i]);
  addB(S, -1.0f, edge);

  if (variant == SPEC_CHECK_A || variant == SPEC_CHECK_B) {
    // chequered start/finish line across the road
    const int CH = 14;
    float w = (2.0f - 2.0f * EW) / CH;
    for (int i = 0; i < CH; i++)
      addB(S, -1.0f + EW + w * i, ((i + (variant == SPEC_CHECK_B)) & 1) ? COL_WHITE : rgb(24, 24, 28));
  } else if (variant == SPEC_STOP) {
    addB(S, -1.0f + EW, lane);                    // stop line across the road
  } else {
    addB(S, -1.0f + EW, road, TK_ROAD);
    if (seg.flags & SEGF_CROSSWALK) {
      // zebra bars parallel to the driving direction
      const int BARS = 9;
      const float span = 1.80f, bw = 0.12f, step = (span - bw) / (BARS - 1);
      for (int i = 0; i < BARS; i++) {
        float a = -0.90f + step * i;
        addB(S, a, lane);
        addB(S, a + bw, road, TK_ROAD);
      }
    } else if (light) {
      for (int l = 1; l < LANES; l++) {
        float lx = -1.0f + 2.0f * l / LANES;
        addB(S, lx - LW * 0.5f, lane);
        addB(S, lx + LW * 0.5f, road, TK_ROAD);
      }
    }
  }
  addB(S, 1.0f - EW, edge);

  // Right side: from the road edge outwards
  addB(S, 1.0f, R.c[0], R.k[0], R.sea[0]);
  for (int i = 1; i < R.n; i++) addB(S, R.u[i - 1], R.c[i], R.k[i], R.sea[i]);
}

// ═══════════════════════════════════════════════════════════════
//  ROAD ROWS: texture sources
// ═══════════════════════════════════════════════════════════════
struct KindSrc {
  const RowTex* T;
  float K;              // texels per road half-width
  float sgn, lat0;      // u = (sgn * lat - lat0) * K + uOff
  float uOff;
  float Kv, vOff;       // rows per world unit, offset (animation)
  uint16_t pal[TS_COUNT];
  uint16_t flat;        // average colour (for the glitter split)
};
static KindSrc sK[TK_COUNT];
static GfxTexRow sRows[TK_COUNT - 1];

static int rowTexOfMat(int mat) {
  switch (mat) {
    case MAT_SAND:  return RT_SAND;
    case MAT_WATER: return RT_WATER;
    case MAT_ROCK:  return RT_ROCK;
    case MAT_SNOW:  return RT_SNOW;
    case MAT_DIRT:  return RT_DIRT;
    default:        return RT_GRASS;
  }
}

// Map + palettes of every kind a segment uses (palettes fogged by a32)
struct SegTex {
  uint32_t used;        // bit per kind
  int      mat[2];
  uint16_t base[TK_COUNT];
  uint16_t kerb;
  uint32_t asphaltShow, streetShow;
};

static void kindPalette(int k, const SegTex& st, uint16_t fogc, int a32) {
  KindSrc& S = sK[k];
  switch (k) {
    case TK_ROAD:     tex_asphaltPalette(S.pal, st.base[k], st.asphaltShow, fogc, a32); break;
    case TK_STREET_L:
    case TK_STREET_R: tex_asphaltPalette(S.pal, st.base[k], st.streetShow, fogc, a32); break;
    case TK_PAVE_L:
    case TK_PAVE_R:   tex_pavementPalette(S.pal, st.base[k], st.kerb, fogc, a32); break;
    case TK_GND_L:    tex_rowPalette(S.pal, rowTexOfMat(st.mat[0]), st.base[k], fogc, a32); break;
    case TK_GND_R:    tex_rowPalette(S.pal, rowTexOfMat(st.mat[1]), st.base[k], fogc, a32); break;
    case TK_WATER_L:
    case TK_WATER_R:  tex_rowPalette(S.pal, RT_WATER, st.base[k], fogc, a32); break;
    case TK_GLINT: {
      tex_rowPalette(S.pal, RT_WATER, st.base[k], fogc, a32);
      const int g = (int)(sGlint * 32.0f);
      S.pal[TS_LIGHT] = add565(S.pal[TS_LIGHT], scale565(sGlintCol, (uint32_t)(g * 3 / 4)));
      S.pal[TS_TRIM]  = add565(S.pal[TS_TRIM], scale565(sGlintCol, (uint32_t)g));
      S.pal[TS_BASE]  = add565(S.pal[TS_BASE], scale565(sGlintCol, (uint32_t)(g / 5)));
      break;
    }
    default: break;
  }
  S.flat = S.pal[TS_BASE];
}

static void setupSegTex(SegTex& st, const Segment& seg, int sIdx) {
  const bool light = ((sIdx / RUMBLE_LEN) & 1) == 0;
  const int k = light ? 0 : 1;
  st.used = 0;
  st.mat[0] = seg.mat(0);
  st.mat[1] = seg.mat(1);
  const uint16_t road = light ? pal.roadL : pal.roadD;
  st.base[TK_ROAD] = seg.isTunnel() ? darkenCol(road, 0.72f) : road;
  st.base[TK_STREET_L] = st.base[TK_STREET_R] = darkenCol(road, 0.86f);
  st.base[TK_PAVE_L] = st.base[TK_PAVE_R] = pal.ground[MAT_SIDEWALK][k];
  st.kerb = light ? pal.kerbL : lerpCol(pal.kerbL, pal.kerbD, 0.35f);
  for (int s = 0; s < 2; s++) {
    int m = st.mat[s] < MAT_COUNT ? st.mat[s] : 0;
    bool flatBand = (m == MAT_SAND || m == MAT_SNOW || m == MAT_WATER);
    st.base[TK_GND_L + s] = pal.ground[m][flatBand ? 0 : k];
    st.base[TK_WATER_L + s] = sSea;
  }
  st.base[TK_GLINT] = sSea;
  // road details per 4-segment texture period: patches, seams, cracks, oil
  uint32_t h = hash32((uint32_t)(sIdx >> 2) * 0x9E3779B1u + 0xA5u);
  st.asphaltShow = (1u << TS_DARK) | (1u << TS_SHADE) | (1u << TS_BASE) | (1u << TS_LIGHT) | (1u << TS_TRIM);
  if ((h & 7) < 2) st.asphaltShow |= (1u << TS_ALT) | (1u << TS_ALT2);
  if (((h >> 3) & 7) < 2) st.asphaltShow |= 1u << TS_ACC;
  if (((h >> 6) & 7) < 3) st.asphaltShow |= 1u << TS_FRAME;
  if (((h >> 9) & 7) < 2) st.asphaltShow |= 1u << TS_ACC2;
  st.streetShow = st.asphaltShow | (1u << TS_FRAME) | (1u << TS_ACC2);

  // texture mappings
  const float vAnim = sTime * 3.0f, uAnim = sTime * 1.3f;
  for (int kk = 1; kk < TK_COUNT; kk++) {
    KindSrc& S = sK[kk];
    S.sgn = 1.0f; S.lat0 = 0.0f; S.uOff = 0.0f; S.vOff = 0.0f;
    switch (kk) {
      case TK_ROAD: case TK_STREET_L: case TK_STREET_R:
        S.T = &tex_row(RT_ASPHALT); S.K = TEX_ASPHALT_U; S.Kv = TEX_ASPHALT_V;
        if (kk == TK_STREET_L) S.sgn = -1.0f;
        break;
      case TK_PAVE_L: case TK_PAVE_R:
        S.T = &tex_row(RT_PAVEMENT); S.K = TEX_PAVE_U; S.Kv = TEX_PAVE_V;
        S.sgn = kk == TK_PAVE_L ? -1.0f : 1.0f; S.lat0 = 1.0f;
        break;
      case TK_GND_L: case TK_GND_R:
        S.T = &tex_row(rowTexOfMat(st.mat[kk - TK_GND_L])); S.K = TEX_GROUND_U; S.Kv = TEX_GROUND_V;
        break;
      default:   // water and glitter: waves roll in towards the shore
        S.T = &tex_row(RT_WATER); S.K = TEX_GROUND_U * 0.75f; S.Kv = TEX_GROUND_V * 0.6f;
        S.uOff = (kk == TK_WATER_R) ? -uAnim : uAnim;
        S.vOff = vAnim;
        if (kk == TK_WATER_R) S.sgn = -1.0f;
        break;
    }
  }
}

// Mip level for a texel density (texels per pixel); TEX_LEVELS = too fine
static inline int mipFor(float d) {
  int l = 0;
  while (l < TEX_LEVELS && d > 1.35f * (float)(1 << l)) l++;
  return l;
}

static void drawBand(const SegView& s, int n, const Segment& seg, int sIdx) {
  int r0 = rowOf(s.f.y), r1 = imin(rowOf(s.n.y), s.clipRow);
  if (r1 <= r0) return;
  float dy = s.n.y - s.f.y;
  if (dy < 1e-3f) return;

  const bool tun = seg.isTunnel();
  // the chequered line starts at the near edge of the SEGF_START segment
  // and covers it and the next one: 4 rows of half a segment
  const bool start0 = (seg.flags & SEGF_START) != 0;
  const bool start = start0 || (segAt(sIdx - 1).flags & SEGF_START) != 0;
  const bool stop = !(seg.flags & SEGF_CROSSWALK) && (segAt(sIdx + 1).flags & SEGF_CROSSWALK) && !tun;
  static BandSpec spec[SPEC_COUNT];
  buildSpec(spec[SPEC_NORMAL], seg, sIdx, SPEC_NORMAL);
  if (start) { buildSpec(spec[SPEC_CHECK_A], seg, sIdx, SPEC_CHECK_A); buildSpec(spec[SPEC_CHECK_B], seg, sIdx, SPEC_CHECK_B); }
  if (stop) buildSpec(spec[SPEC_STOP], seg, sIdx, SPEC_STOP);
  const uint16_t fogc = tun ? COL_BLACK : pal.fog;

  // textures for the rows of this segment
  const bool texOn = textures_ready() && s.n.z < TEX_MAX_Z;
  static SegTex st;
  if (texOn) {
    setupSegTex(st, seg, sIdx);
    for (int v = 0; v < SPEC_COUNT; v++) {
      if ((v == SPEC_CHECK_A || v == SPEC_CHECK_B) && !start) continue;
      if (v == SPEC_STOP && !stop) continue;
      for (int r = 0; r <= spec[v].n; r++) st.used |= 1u << spec[v].k[r];
    }
    st.used &= ~1u;
    if (sGlint > 0 && (st.used & ((1u << TK_WATER_L) | (1u << TK_WATER_R)))) st.used |= 1u << TK_GLINT;
  }

  const float invZf = 1.0f / s.f.z, invZn = 1.0f / s.n.z;
  const float dIz = invZn - invZf;
  const float zNear = roadBZ[n];               // unclipped near boundary (camera z)
  int lastA = -1, lastSpec = -1, lastPalA = -1;
  float b[MAXB + 4];
  uint16_t c[MAXB + 5];
  uint8_t sel[MAXB + 5];
  bool active[TK_COUNT];

  for (int y = r0; y < r1; y++) {
    float t = (y + 0.5f - s.f.y) / dy;              // 0 far .. 1 near
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    const float cx = s.f.x + (s.n.x - s.f.x) * t;
    const float hw = s.f.w + (s.n.w - s.f.w) * t;
    const float iz = invZf + dIz * t;
    const float z = 1.0f / iz;
    int a = fogAlpha(tun ? z * 1.6f : z);
    if (tun && a > 28) a = 28;
    const float zLoc = z - zNear;                    // 0 .. SEG_LEN from the near edge
    int si = SPEC_NORMAL;
    if (start) si = ((int)((zLoc + (start0 ? 0.0f : SEG_LEN)) * (2.0f / SEG_LEN)) & 1) ? SPEC_CHECK_B : SPEC_CHECK_A;
    else if (stop && zLoc > SEG_LEN * 0.50f && zLoc < SEG_LEN * 0.78f) si = SPEC_STOP;
    const BandSpec& S = spec[si];
    const bool rowTex = texOn && z < TEX_MAX_Z && hw > 6.0f;
    const int aSea = (int)(a * SEA_FOG + 0.5f);
    if (a != lastA || si != lastSpec) {
      for (int k = 0; k <= S.n; k++) c[k] = fogMix(S.c[k], fogc, S.sea[k] ? aSea : a);
      lastA = a;
      lastSpec = si;
    }
    if (rowTex && a != lastPalA) {
      for (int k = 1; k < TK_COUNT; k++)
        if (st.used & (1u << k)) {
          bool seaK = (k == TK_WATER_L || k == TK_WATER_R || k == TK_GLINT);
          kindPalette(k, st, fogc, seaK ? aSea : a);
        }
      lastPalA = a;
    }
    for (int k = 0; k < S.n; k++) b[k] = cx + S.u[k] * hw;
    int nr = S.n + 1;

    if (!rowTex) {
      gfx_rowRegions(y, b, c, nr);
      continue;
    }
    // per-kind texture row for this scanline (mip by texel density)
    const float dzdy = z * z * dIz / dy;             // world units per screen row
    const float zWorld = (float)sIdx * SEG_LEN + zLoc;
    for (int k = 1; k < TK_COUNT; k++) {
      active[k] = false;
      if (!(st.used & (1u << k))) continue;
      KindSrc& K = sK[k];
      const float du = K.K / hw;
      const float dv = K.Kv * dzdy;
      const int l = mipFor(fmaxF(du, dv));
      if (l >= TEX_LEVELS) continue;
      const int W = (1 << K.T->wLog2) >> l, H = (1 << K.T->hLog2) >> l;
      float vt = (zWorld * K.Kv + K.vOff) / (float)(1 << l);
      int vi = (int)floorf(vt) & (H - 1);
      float lat0 = (0.5f - cx) / hw;                   // lateral offset of column 0
      float u0 = ((K.sgn * lat0 - K.lat0) * K.K + K.uOff) / (float)(1 << l);
      u0 = fmodf(u0, (float)W);
      if (u0 < 0) u0 += (float)W;
      GfxTexRow& R = sRows[k - 1];
      R.row = K.T->lev[l] + vi * W;
      R.u0 = (uint32_t)(u0 * 65536.0f);
      R.du = (int32_t)(K.sgn * du / (float)(1 << l) * 65536.0f);
      R.mask = (uint32_t)(W - 1);
      R.pal = K.pal;
      active[k] = true;
    }
    for (int r = 0; r < nr; r++) sel[r] = active[S.k[r]] ? S.k[r] : TK_NONE;

    // sun glitter column on the water: split the water region
    if (sGlint > 0 && active[TK_GLINT] && y > SCR_CY) {
      const float gw = 3.0f + (y - SCR_CY) * 0.40f;
      const float g0 = sSunX - gw, g1 = sSunX + gw;
      for (int r = 0; r < nr; r++) {
        if (sel[r] != TK_WATER_L && sel[r] != TK_WATER_R) continue;
        float x0 = r > 0 ? b[r - 1] : -1e9f, x1 = r < nr - 1 ? b[r] : 1e9f;
        float a0 = fmaxF(x0, g0), a1 = fminF(x1, g1);
        if (a1 <= a0 + 0.5f || nr + 2 > MAXB + 4) continue;
        // copy the glitter mapping from this water kind
        sRows[TK_GLINT - 1] = sRows[sel[r] - 1];
        sRows[TK_GLINT - 1].pal = sK[TK_GLINT].pal;
        // regions: [x0, a0) water | [a0, a1) glint | [a1, x1) water
        for (int q = nr - 1; q > r; q--) { c[q + 2] = c[q]; sel[q + 2] = sel[q]; }
        for (int q = nr - 2; q >= r; q--) b[q + 2] = b[q];
        b[r] = a0;
        b[r + 1] = a1;
        c[r + 1] = sK[TK_GLINT].flat;
        sel[r + 1] = TK_GLINT;
        c[r + 2] = c[r];
        sel[r + 2] = sel[r];
        nr += 2;
        break;
      }
    }
    gfx_rowRegionsTex(y, b, c, nr, sel, sRows);
  }
}

// Beyond the draw distance (horizon band): fog, except the sea, which keeps
// its colour out to a darker horizon line on each side where it lies.
static void drawFarGround(int groundEnd, int lastValid, int baseIdx) {
  const int horizon = SCR_CY;
  if (groundEnd <= horizon) return;
  int vx = SCR_CX;
  bool sea[2] = { false, false };
  if (lastValid >= 0) {
    const Segment& fs = segAt(baseIdx + lastValid);
    vx = (int)iclamp((int)sv[lastValid].f.x, -1, SCR_W + 1);
    for (int side = 0; side < 2; side++) {
      int m = fs.mat(side);
      sea[side] = !fs.isTunnel() && (m == MAT_SAND || m == MAT_WATER);
    }
  }
  const uint16_t water = sSea;
  const uint16_t seaFar = lerpCol(water, pal.fog, 0.58f);
  const uint16_t seaHor = lerpCol(water, pal.skyHorizon, 0.72f);
  for (int y = horizon; y < groundEnd; y++) {
    float t = (float)(y - horizon) / (float)imax(1, groundEnd - horizon);
    if (!sea[0] && !sea[1]) { gfx_hspan(y, 0, SCR_W, pal.fog); continue; }
    if (sea[0]) gfx_ditherRow(y, 0, vx, seaHor, seaFar, t);
    else gfx_hspan(y, 0, vx, pal.fog);
    if (sea[1]) gfx_ditherRow(y, vx, SCR_W, seaHor, seaFar, t);
    else gfx_hspan(y, vx, SCR_W, pal.fog);
  }
  // glitter on the far sea under the sun
  if (sGlint > 0 && (sea[0] || sea[1]) && sSunX > -20 && sSunX < SCR_W + 20) {
    for (int y = horizon; y < groundEnd; y++) {
      bool left = sSunX < vx;
      if ((left && !sea[0]) || (!left && !sea[1])) continue;
      int w = 2 + (y - horizon) / 3;
      int a = (int)(sGlint * 14.0f);
      uint32_t h = hash32((uint32_t)y * 0x9E37u + (uint32_t)(sTime * 6.0f));
      int jx = (int)(h & 3) - 1;
      gfx_hspanMode(y, sSunX - w + jx, sSunX + w + jx, sGlintCol, GFX_ADD, a);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  TUNNEL
// ═══════════════════════════════════════════════════════════════
static inline float ceilY(const ProjPt& p, float worldY) {
  return SCR_CY - p.scale * (worldY + TUNNEL_H - roadCam.camY) * SCR_CY;
}

static int tunnelVariant() {
  return colorTheme == THEME_COAST ? 1 : (colorTheme == THEME_MOUNTAIN ? 2 : 0);
}

static void drawTunnelSeg(const SegView& s, int sIdx, bool farthest) {
  float cyN = ceilY(s.n, s.nyW), cyF = ceilY(s.f, s.fyW);
  float lN = s.n.x - s.n.w * WALL_K, rN = s.n.x + s.n.w * WALL_K;
  float lF = s.f.x - s.f.w * WALL_K, rF = s.f.x + s.f.w * WALL_K;
  float gN = s.n.y, gF = s.f.y;
  float dark = fminF(s.fog * 1.4f, 0.92f);   // the tunnel fades to black, not to haze

  if (farthest)   // tunnel continues past the draw distance: dark opening
    gfx_fillQuad(lF, cyF, rF, cyF, rF, gF, lF, gF, COL_BLACK);

  // Ceiling: rough concrete lining (texture, constant depth per row) with
  // a dark rib at the near edge of every segment
  bool light = (sIdx & 1) == 0;
  uint16_t ceil = darkenCol(light ? pal.tunnelCeil : lerpCol(pal.tunnelCeil, COL_BLACK, 0.15f), 1.0f - dark);
  const float dyC = cyF - cyN;              // near edge is higher on screen
  if (textures_ready() && dyC > 0.5f && s.n.z < TEX_MAX_Z) {
    static uint16_t PC[TS_COUNT], PR[TS_COUNT];
    tex_rowPalette(PC, RT_ROCK, light ? pal.tunnelCeil : lerpCol(pal.tunnelCeil, COL_BLACK, 0.15f), COL_BLACK,
                   (int)(dark * 32.0f + 0.5f));
    for (int i = 0; i < TS_COUNT; i++) PR[i] = darkenCol(PC[i], 0.6f);
    const RowTex& T = tex_row(RT_ROCK);
    const int y0 = imax(rowOf(cyN), gfx_cy0), y1 = imin(rowOf(cyF), gfx_cy1);
    const float izN = 1.0f / s.n.z, izF = 1.0f / s.f.z, zNear = s.f.z - SEG_LEN;
    for (int y = y0; y < y1; y++) {
      const float t = clampF((y + 0.5f - cyN) / dyC, 0.0f, 1.0f);   // 0 near .. 1 far
      const float iz = izN + (izF - izN) * t, z = 1.0f / iz;
      const float xl = lN + (lF - lN) * t, xr = rN + (rF - rN) * t;
      const float cx = (xl + xr) * 0.5f, hw = (xr - xl) * (0.5f / WALL_K);
      if (hw < 1.0f) continue;
      const float du = TEX_GROUND_U / hw;
      const float dzdy = z * z * (izN - izF) / dyC;
      const int l = mipFor(fmaxF(du, TEX_GROUND_V * dzdy));
      const float zLoc = z - zNear;
      const uint16_t* P = (zLoc < SEG_LEN * 0.16f) ? PR : PC;
      if (l >= TEX_LEVELS) { gfx_hspan(y, colOf(xl), colOf(xr), P[TS_BASE]); continue; }
      const int W = (1 << T.wLog2) >> l, H = (1 << T.hLog2) >> l;
      const int vi = (int)floorf(((float)sIdx * SEG_LEN + zLoc) * TEX_GROUND_V / (float)(1 << l)) & (H - 1);
      const int xa = colOf(xl), xb = colOf(xr);
      float u0 = ((xa + 0.5f - cx) / hw * TEX_GROUND_U) / (float)(1 << l);
      u0 = fmodf(u0, (float)W);
      if (u0 < 0) u0 += (float)W;
      gfx_texSpan(y, xa, xb, T.lev[l] + vi * W, (uint32_t)(u0 * 65536.0f),
                  (int32_t)(du / (float)(1 << l) * 65536.0f), (uint32_t)(W - 1), P);
    }
  } else {
    gfx_fillQuad(lN, cyN, rN, cyN, rF, cyF, lF, cyF, ceil);
    float m = 0.18f;
    float rlN = lN + (lF - lN) * m, rrN = rN + (rF - rN) * m, ryN = cyN + (cyF - cyN) * m;
    gfx_fillQuad(lN, cyN, rN, cyN, rrN, ryN, rlN, ryN, darkenCol(ceil, 0.6f));
  }

  // Walls: tiled lining (texture), lit by the tunnel lamps
  if (textures_ready()) {
    static uint16_t P[TS_COUNT];
    const int f32 = (int)(dark * 32.0f + 0.5f);
    tex_buildPalette(P, tex_colors(WT_TUNNEL, tunnelVariant()), TF_TUNNEL, COL_BLACK, f32);
    const WallTex& T = tex_wall(WT_TUNNEL);
    WallJob J;
    memset(&J, 0, sizeof(J));
    J.tex = &T;
    J.vTot = (float)T.rows;
    J.capV = (float)T.cap;
    J.baseV = (float)T.base;
    J.dayState = -1;
    J.pal = P;
    J.palBase = P;
    J.farCol = P[TS_BASE];
    const float nearZ = s.f.z - SEG_LEN;
    const float frac = clampF((s.n.z - nearZ) / SEG_LEN, 0.0f, 1.0f);
    const float uN = ((float)sIdx + frac) * (TEX_U_PER_SEG * 0.5f), uF = ((float)sIdx + 1.0f) * (TEX_U_PER_SEG * 0.5f);
    J.iza = 1.0f / s.n.z; J.izb = 1.0f / s.f.z;
    J.uza = uN * J.iza; J.uzb = uF * J.izb;
    J.gya = gN; J.gyb = gF; J.tya = cyN; J.tyb = cyF;
    J.xa = lN; J.xb = lF;
    drawWallColumns(J);
    J.xa = rN; J.xb = rF;
    drawWallColumns(J);
  } else {
    uint16_t wall = darkenCol(light ? pal.tunnelWallL : pal.tunnelWallD, 1.0f - dark);
    gfx_fillQuad(lN, gN, lF, gF, lF, cyF, lN, cyN, wall);
    gfx_fillQuad(rN, gN, rN, cyN, rF, cyF, rF, gF, wall);
  }

  // Ceiling lights every 4 segments: lamp strip + light pool on the road
  if ((sIdx % 4) == 0) {
    float mN = s.n.x, mF = s.f.x, wN = s.n.w * 0.10f, wF = s.f.w * 0.10f;
    uint16_t lamp = lerpCol(pal.tunnelLight, COL_BLACK, dark * 0.8f);
    float hN = cyN + (cyF - cyN) * 0.55f, hW = mN + (mF - mN) * 0.55f;
    gfx_fillQuad(mN - wN, cyN, mN + wN, cyN, hW + wF, hN, hW - wF, hN, lamp);
    int pa = (int)(10 * (1.0f - dark));
    if (pa > 0) {
      int py = (int)(gN * 0.6f + gF * 0.4f);
      gfx_fillEllipse((int)(mN * 0.6f + mF * 0.4f), py, (int)(s.n.w * 0.8f),
                      (int)fmaxF(1.0f, (gN - gF) * 0.6f), pal.tunnelLight, GFX_ADD, pa);
      gfx_glow((int)mN, (int)(cyN + (cyF - cyN) * 0.3f), (int)fminF(40.0f, fmaxF(3.0f, wN * 2.5f)),
               pal.tunnelLight, (int)(14 * (1.0f - dark)));
    }
  }
}

// Tunnel mouth at the near boundary of the first tunnel segment. City: a
// concrete underpass face with a parapet and railing; coast / mountain: a
// rock face with a jagged top around a concrete portal ring.
static void drawPortal(const SegView& s, int sIdx) {
  const float sc = s.n.scale, gy = s.n.y;
  const float cy = ceilY(s.n, s.nyW);
  const bool city = colorTheme == THEME_CITY;
  const float topY = SCR_CY - sc * (s.nyW + TUNNEL_H * (city ? 1.75f : 2.3f) - roadCam.camY) * SCR_CY;
  const float l = s.n.x - s.n.w * WALL_K, r = s.n.x + s.n.w * WALL_K;
  const float halfBase = s.n.w * (city ? 9.0f : 7.5f), halfTop = s.n.w * (city ? 9.0f : 3.0f);
  const float span = gy - topY;
  if (span < 1) return;
  const int f32 = (int)(s.fog * 32.0f + 0.5f);
  const int face = TF_FRONT;
  static uint16_t P[TS_COUNT];
  const int tid = city ? WT_WALL : WT_CLIFF;
  const int var = city ? 0 : (colorTheme == THEME_COAST ? 2 : 1);
  const float texScale = city ? 1.0f : 0.5f;      // big rock faces: larger features
  tex_buildPalette(P, tex_colors(tid, var), face, pal.fog, f32);

  if (textures_ready()) {
    const WallTex& T = tex_wall(tid);
    const float floorPS = BUILDING_FLOOR_H * BLD_V_SCALE;
    const float tpp = TEX_V_PER_FLOOR / (floorPS * sc) * texScale;   // texels per pixel
    int l0 = 0;
    while (l0 < TEX_LEVELS - 1 && tpp > 1.6f * (float)(1 << l0)) l0++;
    const int R = T.rows >> l0, W = (1 << T.wLog2) >> l0;
    const float upx = ((float)TEX_U_PER_SEG / SEG_LEN) / (sc * SCR_CX) * texScale;   // texels per px across
    const int xa = imax(gfx_cx0, (int)ceilf(s.n.x - halfBase - 0.5f));
    const int xb = imin(gfx_cx1, (int)ceilf(s.n.x + halfBase - 0.5f));
    const float sclv = 65536.0f / (float)(1 << l0);
    for (int x = xa; x < xb; x++) {
      const float d = fabsf(x + 0.5f - s.n.x);
      float top = topY;
      if (d > halfTop) top += (d - halfTop) / fmaxF(1.0f, halfBase - halfTop) * span;
      const float u = (x + 0.5f - s.n.x) * upx + (float)sIdx * 3.0f + 4096.0f;
      if (!city) {
        // jagged rock skyline
        float jx = u * 0.15f;
        int i = (int)floorf(jx);
        float f = jx - i;
        float na = (hash32((uint32_t)i * 0x9E37u + 0x77u) & 255) / 255.0f;
        float nb = (hash32((uint32_t)(i + 1) * 0x9E37u + 0x77u) & 255) / 255.0f;
        top += (na + (nb - na) * f) * span * 0.08f;
      }
      const float bot = (x + 0.5f > l && x + 0.5f < r) ? cy : gy;
      int y0 = imax(rowOf(top), gfx_cy0), y1 = imin(rowOf(bot), gfx_cy1);
      if (y1 <= y0) continue;
      const int ui = ((int)u >> l0) & (W - 1);
      const uint8_t* col = T.lev[l0] + ui * R;
      // top 16 texels from the cap module, then the body module; every
      // body tile row is shifted sideways so the repeat does not show
      const float ppt = 1.0f / tpp;
      int yc = imin(y1, rowOf(top + (float)T.cap * ppt));
      float v0 = (y0 + 0.5f - top) * tpp;
      if (yc > y0) gfx_texColumn(x, y0, yc, col, (uint32_t)(v0 * sclv), (int32_t)(tpp * sclv),
                                 (uint32_t)((T.cap >> l0) - 1), P);
      const float B = (float)T.body;
      const int yBody = imax(y0, yc);
      if (city) {   // concrete panels: one wrapped call
        float v1 = (yBody + 0.5f - top) * tpp - (float)T.cap;
        if (v1 < 0) v1 = 0;
        gfx_texColumn(x, yBody, y1, col + (T.cap >> l0), (uint32_t)(v1 * sclv), (int32_t)(tpp * sclv),
                      (uint32_t)((T.body >> l0) - 1), P);
        continue;
      }
      // only the tile rows this column shows
      const int kFirst = imax(0, (int)(((yBody + 0.5f - top) * tpp - (float)T.cap) / B));
      for (int k = kFirst; k < kFirst + 64; k++) {
        const float w0 = (float)T.cap + B * (float)k;
        const int ya = imax(yBody, rowOf(top + w0 * ppt));
        const int yb = imin(y1, rowOf(top + (w0 + B) * ppt));
        if (ya >= y1) break;
        if (yb <= ya) continue;
        const int uk = (((int)u + (int)(hash32((uint32_t)k * 0x2F1u + (uint32_t)sIdx) & 31)) >> l0) & (W - 1);
        float v1 = (ya + 0.5f - top) * tpp - w0;
        if (v1 < 0) v1 = 0;
        gfx_texColumn(x, ya, yb, T.lev[l0] + uk * R + (T.cap >> l0), (uint32_t)(v1 * sclv),
                      (int32_t)(tpp * sclv), (uint32_t)((T.body >> l0) - 1), P);
      }
    }
  } else {
    gfx_fillQuad(s.n.x - halfTop, topY, s.n.x + halfTop, topY, s.n.x + halfBase, gy, s.n.x - halfBase, gy, P[TS_BASE]);
  }

  // concrete portal ring around the mouth, with a lit soffit edge
  uint16_t ringC = fogF(pal.portal, s.fog), ringD = darkenCol(ringC, 0.70f);
  float fw = s.n.w * 0.16f;
  gfx_fillQuad(l - fw, cy - fw * 1.3f, r + fw, cy - fw * 1.3f, r + fw, cy, l - fw, cy, ringC);
  gfx_fillQuad(l - fw, cy, l, cy, l, gy, l - fw, gy, ringD);
  gfx_fillQuad(r, cy, r + fw, cy, r + fw, gy, r, gy, ringD);
  if (city) {
    // parapet + railing on top of the underpass
    float pt = topY - s.n.w * 0.10f;
    gfx_fillQuad(s.n.x - halfTop, pt, s.n.x + halfTop, pt, s.n.x + halfTop, topY, s.n.x - halfTop, topY, P[TS_TRIM]);
    float rh = s.n.w * 0.28f, rw = fmaxF(1.0f, s.n.w * 0.025f);
    uint16_t rail = fogF(rgb(60, 64, 72), s.fog);
    gfx_fillQuad(s.n.x - halfTop, pt - rh, s.n.x + halfTop, pt - rh, s.n.x + halfTop, pt - rh + rw,
                 s.n.x - halfTop, pt - rh + rw, rail);
    float step = fmaxF(3.0f, s.n.w * 0.35f);
    for (float x = s.n.x - halfTop; x < s.n.x + halfTop; x += step) {
      if (x < gfx_cx0 - 2 || x > gfx_cx1 + 2) continue;
      gfx_fillQuad(x, pt - rh, x + rw, pt - rh, x + rw, pt, x, pt, rail);
    }
  }
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
  float armX = baseX - side * p.w * 0.34f;
  uint16_t pole = fogF(pal.lampPole, fog);
  uint16_t poleL = fogF(lerpCol(pal.lampPole, COL_WHITE, 0.25f), fog);
  // base plinth, tapered pole with a lit edge
  float bw = pw * 1.8f, bh = fmaxF(1.0f, poleH * 0.06f);
  gfx_fillQuad(baseX - bw * 0.5f, p.y, baseX + bw * 0.5f, p.y, baseX + bw * 0.5f, p.y - bh, baseX - bw * 0.5f, p.y - bh, pole);
  gfx_fillQuad(baseX - pw * 0.5f, p.y, baseX + pw * 0.5f, p.y, baseX + pw * 0.35f, topY, baseX - pw * 0.35f, topY, pole);
  if (pw >= 2.0f)
    gfx_fillQuad(baseX - pw * 0.5f, p.y, baseX - pw * 0.1f, p.y, baseX - pw * 0.05f, topY, baseX - pw * 0.35f, topY, poleL);
  // arm over the road, rising slightly
  float ah = fmaxF(1.0f, pw * 0.7f);
  gfx_fillQuad(baseX, topY, armX, topY - ah * 0.6f, armX, topY + ah * 0.4f, baseX, topY + ah, pole);
  // luminaire: housing + lit lens underneath
  float hw = fmaxF(1.5f, pw * 2.0f);
  float hy0 = topY - ah * 0.8f, hy1 = topY + ah * 0.9f;
  gfx_fillQuad(armX - hw, hy0, armX + hw, hy0, armX + hw * 0.8f, hy1, armX - hw * 0.8f, hy1, pole);
  uint16_t lens = lerpCol(fogF(rgb(200, 200, 190), fog), pal.lampLight, pal.night);
  gfx_fillQuad(armX - hw * 0.8f, hy1, armX + hw * 0.8f, hy1, armX + hw * 0.6f, hy1 + ah * 0.8f,
               armX - hw * 0.6f, hy1 + ah * 0.8f, lens);
  if (pal.night > 0.15f) {
    int a = (int)(26 * pal.night * (1.0f - fog * 0.7f));
    gfx_glow((int)armX, (int)(hy1 + ah * 0.5f), (int)fminF(30.0f, fmaxF(3, hw * 3.2f)), pal.lampLight, a);
    // light pool on the road under the lamp
    gfx_fillEllipse((int)armX, (int)p.y, (int)(p.w * 0.60f), (int)fmaxF(1, p.w * 0.075f),
                    pal.lampLight, GFX_ADD, (int)(7 * pal.night * (1.0f - fog)));
  }
}

// Guard rail along one road edge over the whole segment: W-beam with a lit
// top, a groove and a shadowed bottom, posts with reflectors (lit at night)
static void drawRail(const SegView& s, int side, int sIdx) {
  const float H0 = 120.0f, H1 = 300.0f;      // beam bottom / top (world units)
  float xN = s.n.x + side * s.n.w * RAIL_X, xF = s.f.x + side * s.f.w * RAIL_X;
  float bN = s.n.y - s.n.scale * H0 * SCR_CY, tN = s.n.y - s.n.scale * H1 * SCR_CY;
  float bF = s.f.y - s.f.scale * H0 * SCR_CY, tF = s.f.y - s.f.scale * H1 * SCR_CY;
  if (fmaxF(xN, xF) < -4 || fminF(xN, xF) > SCR_W + 4) return;
  const uint16_t steel = lerpCol(rgb(190, 196, 204), pal.fog, 0.10f);
  uint16_t lit = fogF(lerpCol(steel, COL_WHITE, 0.25f * (1.0f - pal.night)), s.fog);
  uint16_t mid = fogF(darkenCol(steel, 0.78f), s.fog);
  uint16_t dark = fogF(darkenCol(steel, 0.45f), s.fog);
  // posts (behind the beam) every segment
  float pw = fmaxF(0.6f, s.n.scale * 45.0f * SCR_CX);
  float pTop = s.n.y - s.n.scale * (H1 + 20.0f) * SCR_CY;
  gfx_fillQuad(xN - pw, s.n.y, xN + pw, s.n.y, xN + pw, pTop, xN - pw, pTop, dark);
  // beam: three horizontal bands
  float m1N = tN + (bN - tN) * 0.38f, m1F = tF + (bF - tF) * 0.38f;
  float m2N = tN + (bN - tN) * 0.62f, m2F = tF + (bF - tF) * 0.62f;
  gfx_fillQuad(xN, tN, xF, tF, xF, m1F, xN, m1N, lit);
  gfx_fillQuad(xN, m1N, xF, m1F, xF, m2F, xN, m2N, dark);
  gfx_fillQuad(xN, m2N, xF, m2F, xF, bF, xN, bN, mid);
  // reflector on the post (amber on the right, white on the left)
  if ((sIdx & 1) == 0 && pw >= 1.0f) {
    uint16_t rc = side > 0 ? rgb(255, 170, 40) : rgb(240, 240, 255);
    float ry = tN + (bN - tN) * 0.5f;
    float rs = fmaxF(1.0f, pw * 0.9f);
    gfx_fillRect((int)(xN - rs * 0.5f), (int)(ry - rs * 0.5f), (int)fmaxF(1, rs), (int)fmaxF(1, rs),
                 lerpCol(fogF(darkenCol(rc, 0.7f), s.fog), rc, pal.night));
    if (pal.night > 0.3f && s.fog < 0.8f)
      gfx_glow((int)xN, (int)ry, (int)fmaxF(2.0f, rs * 2.5f), rc, (int)(16 * pal.night * (1.0f - s.fog)));
  }
}

// Start/finish gantry over the line: lattice towers, a truss, the START /
// FINISH banner (texture) and the start lights (mirroring the countdown)
static void drawGantry(const ProjPt& p, float fog) {
  const float H0 = 2250.0f, H1 = 2900.0f, HT = 3200.0f, X = 1.34f;
  const float sc = p.scale;
  if (sc * HT * SCR_CY < 4) return;
  const float lx = p.x - p.w * X, rx = p.x + p.w * X;
  const float yb = p.y - sc * H0 * SCR_CY, yt = p.y - sc * H1 * SCR_CY, ytt = p.y - sc * HT * SCR_CY;
  const float pw = fmaxF(1.0f, sc * 130.0f * SCR_CX);
  const uint16_t steel = fogF(rgb(150, 156, 166), fog), steelD = fogF(rgb(70, 74, 84), fog);
  // towers: two chords and cross bracing
  for (int s = -1; s <= 1; s += 2) {
    float cxp = s < 0 ? lx - pw : rx + pw;
    gfx_fillQuad(cxp - pw, p.y, cxp - pw * 0.4f, p.y, cxp - pw * 0.4f, ytt, cxp - pw, ytt, steel);
    gfx_fillQuad(cxp + pw * 0.4f, p.y, cxp + pw, p.y, cxp + pw, ytt, cxp + pw * 0.4f, ytt, steelD);
    if (pw >= 2.0f) {
      float stepY = fmaxF(3.0f, pw * 2.0f), lw = fmaxF(1.0f, pw * 0.25f);
      for (float y = p.y; y > ytt + stepY; y -= stepY)
        gfx_fillQuad(cxp - pw, y, cxp - pw + lw, y, cxp + pw, y - stepY, cxp + pw - lw, y - stepY, steelD);
    }
  }
  // truss over the road: chords + zigzag
  {
    float ch = fmaxF(1.0f, (yt - ytt) * 0.18f);
    gfx_fillQuad(lx - pw * 2, ytt, rx + pw * 2, ytt, rx + pw * 2, ytt + ch, lx - pw * 2, ytt + ch, steel);
    gfx_fillQuad(lx - pw * 2, yt - ch, rx + pw * 2, yt - ch, rx + pw * 2, yt, lx - pw * 2, yt, steelD);
    float bay = fmaxF(4.0f, (yt - ytt) * 1.1f), lw = fmaxF(1.0f, ch * 0.7f);
    int k = 0;
    for (float x = lx; x < rx; x += bay, k++) {
      if (x + bay < gfx_cx0 || x > gfx_cx1) continue;
      float xa = x, xb = fminF(x + bay, rx);
      if (k & 1) gfx_fillQuad(xa, ytt + ch, xa + lw, ytt + ch, xb + lw, yt - ch, xb, yt - ch, steelD);
      else gfx_fillQuad(xa, yt - ch, xa + lw, yt - ch, xb + lw, ytt + ch, xb, ytt + ch, steelD);
    }
  }
  // banner (texture), hanging under the truss
  const uint8_t* ban = tex_banner((raceRunning && racers[PLAYER_ID].lap >= curCircuit().laps - 1) ? 1 : 0);
  const float bH = yb - yt, bW = rx - lx;
  if (ban && bH >= 2.0f && bW >= 8.0f) {
    uint16_t P[TS_COUNT];
    const uint16_t navy = rgb(18, 24, 70);
    P[TS_DARK] = fogF(navy, fog);
    P[TS_LIGHT] = fogF(rgb(236, 236, 236), fog);
    P[TS_SHADE] = fogF(rgb(16, 16, 18), fog);
    P[TS_TRIM] = fogF(rgb(200, 200, 206), fog);
    P[TS_EMITB] = fogF(lerpCol(rgb(255, 214, 40), rgb(255, 246, 170), pal.night), fog);
    for (int i = 0; i < TS_COUNT; i++)
      if (i != TS_DARK && i != TS_LIGHT && i != TS_SHADE && i != TS_TRIM && i != TS_EMITB) P[i] = P[TS_DARK];
    const int y0 = imax(rowOf(yt), gfx_cy0), y1 = imin(rowOf(yb), gfx_cy1);
    const int x0 = imax(colOf(lx), gfx_cx0), x1 = imin(colOf(rx), gfx_cx1);
    const float du = TEX_BANNER_W / bW;
    for (int y = y0; y < y1; y++) {
      int v = iclamp((int)((y + 0.5f - yt) / bH * TEX_BANNER_H), 0, TEX_BANNER_H - 1);
      float u = (x0 + 0.5f - lx) * du;
      gfx_texSpan(y, x0, x1, ban + v * TEX_BANNER_W, (uint32_t)(u * 65536.0f), (int32_t)(du * 65536.0f),
                  TEX_BANNER_W - 1, P);
    }
    if (pal.night > 0.3f)   // floodlit at night
      gfx_fillQuad(lx, yt, rx, yt, rx, yb, lx, yb, pal.lampLight, GFX_ADD, (int)(6 * pal.night * (1 - fog)));
  }
  // start lights on top of the truss: the same five lights, in the same
  // state, as the countdown gantry of the UI (ui_countdownLightMask)
  {
    float lt = -1.0f;                     // seconds since the countdown began
    const GameState gs = game_state();
    if (gs == GS_COUNTDOWN || gs == GS_DEMO) lt = game_stateTime();
    else if (raceRunning && racers[PLAYER_ID].lap == 0) lt = UI_COUNTDOWN_GO + raceTime;
    bool green = false;
    const int mask = lt >= 0.0f ? ui_countdownLightMask(lt, &green) : 0;
    const float r = fmaxF(1.0f, sc * 100.0f * SCR_CX);
    const float ly = ytt - r * 1.4f;
    gfx_fillQuad(p.x - r * 6.6f, ly - r * 1.4f, p.x + r * 6.6f, ly - r * 1.4f, p.x + r * 6.6f, ytt,
                 p.x - r * 6.6f, ytt, fogF(rgb(26, 26, 30), fog));
    for (int i = 0; i < 5; i++) {
      const float cx = p.x + (i - 2) * r * 2.6f;
      const bool on = green || ((mask >> i) & 1);
      const uint16_t c = green ? rgb(40, 255, 80) : (on ? rgb(255, 40, 30) : rgb(70, 16, 16));
      gfx_fillCircle((int)cx, (int)ly, (int)fmaxF(1.0f, r), fogF(c, fog * 0.5f));
      if (on) gfx_glow((int)cx, (int)ly, (int)fmaxF(3.0f, r * 3.0f), c, 20);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  MAIN PASS
// ═══════════════════════════════════════════════════════════════
void drawRoad(float position, float playerX, float playerZdist, float cameraDepth) {
  if (!sFogInit) initFog();
  if (segCount <= 0) return;
  if (!textures_ready()) textures_init();
  tex_beginFrame();
  buildings_beginFrame();
  sTime = millis() * 0.001f;
  // the sun / moon glitter column follows drawSky's sun position
  {
    sSunX = sky_sunX(skyOffset);
    float low = clampF((pal.sunY - 20.0f) / 80.0f, 0.0f, 1.0f);
    sGlint = (sSunX > -40 && sSunX < SCR_W + 40) ? 0.30f + 0.70f * low : 0.0f;
    if (pal.night > 0.6f) sGlint *= 0.6f;
    sGlintCol = lerpCol(pal.sunGlow, pal.sun, 0.5f);
    sSea = lerpCol(pal.ground[MAT_WATER][0], pal.skyHorizon, 0.26f);
  }

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
  drawFarGround(rowOf(maxy), lastValid, baseIdx);

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

    if (s.roadVis) drawBand(s, n, seg, sIdx);

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

    // Cars of this slice stand at or beyond its near boundary, while the
    // sprites, lamps and the gantry stand ON that boundary: draw the cars
    // first so a tree or lamp at the near edge covers a car behind it.
    cars_drawSlice(n, s.fog, tunnel);

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

    if (tunnel && !prevSeg.isTunnel() && n >= 1) drawPortal(s, sIdx);
  }
  gfx_resetClip();
}
