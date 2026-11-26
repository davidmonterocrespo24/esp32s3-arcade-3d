/*
  ═══════════════════════════════════════════════════════════════
  CIRCUITS — implementation
  1. genLayout(): a parameterised layout (corners, straights, hills,
     hairpins) from a fixed seed, with a flat start straight on both
     sides of the line and an elevation loop that closes exactly.
  2. decorate*(): ground materials, tunnels, buildings, rails, lamps,
     crosswalks and sprites per theme.
  3. buildMinimap(): outline from the curve data.
  ═══════════════════════════════════════════════════════════════
*/

#include "circuits.h"
#include "track.h"
#include "colors.h"
#include "rendering.h"
#include "render_scenery.h"
#include "utils.h"
#include "platform.h"
#include <math.h>
#include <string.h>

const CircuitInfo CIRCUITS[NUM_CIRCUITS] = {
  { "NEON CITY",    "DOWNTOWN AT DUSK",     THEME_CITY,     3, 1.4f, 2.4f },
  { "SUNSET COAST", "SEASIDE HIGHWAY",      THEME_COAST,    3, 0.5f, 1.4f },
  { "ALPINE PASS",  "TUNNELS AND HAIRPINS", THEME_MOUNTAIN, 3, 3.3f, 4.3f },
};

int currentCircuit = 0;
uint8_t minimapX[MINIMAP_PTS], minimapY[MINIMAP_PTS];

const CircuitInfo& curCircuit() {
  return CIRCUITS[iclamp(currentCircuit, 0, NUM_CIRCUITS - 1)];
}

// ═══════════════════════════════════════════════════════════════
//  1. LAYOUT
// ═══════════════════════════════════════════════════════════════
struct Layout {
  uint32_t seed;
  int   segs;                     // total segments (<= MAX_SEGS)
  int   startStraight;            // flat straight after the line
  int   preStraight;              // flat straight before the line (the grid)
  int   closeMin;                 // segments to ease the elevation back to 0
  float curveMin, curveMax;       // |curve| of normal corners
  int   enterMin, enterMax, holdMin, holdMax, leaveMin, leaveMax;
  int   straightMin, straightMax;
  float straightChance;
  float rightBias;                // chance that a corner turns right
  float hillChance, hillMin, hillMax;   // elevation change per section, SEG_LEN units
  float slopeMax;                 // max average slope of a section
  float yMax;                     // keep |elevation| below yMax * SEG_LEN
  int   hairpins;                 // number of very tight corners
  float hairpinCurve;
};

static const Layout LAYOUTS[NUM_CIRCUITS] = {
  // NEON CITY: tight corners, short straights, mild hills
  { 0xC17E0001u, 1900, 24, 44, 50,
    4.0f, 7.0f, 3, 6, 6, 16, 3, 6, 10, 28, 0.38f, 0.60f,
    0.45f, 2.0f, 6.0f, 0.10f, 8.0f, 0, 0.0f },
  // SUNSET COAST: long sweepers, rolling hills
  { 0xC0A57002u, 2200, 24, 44, 60,
    2.0f, 4.5f, 8, 14, 20, 46, 8, 14, 16, 40, 0.26f, 0.62f,
    0.65f, 3.0f, 10.0f, 0.15f, 14.0f, 0, 0.0f },
  // ALPINE PASS: steep climbs and descents, hairpins
  { 0xA1B50003u, 2100, 24, 44, 110,
    3.0f, 6.5f, 4, 8, 8, 22, 4, 8, 6, 20, 0.24f, 0.55f,
    0.85f, 6.0f, 18.0f, 0.24f, 28.0f, 4, 9.5f },
};

static void genLayout(const Layout& L, Rng& rng) {
  track_clear();
  addRoad(0, L.startStraight, 0, 0, 0);
  const int reserve = L.preStraight + L.closeMin;
  bool lastStraight = true;
  int corners = 0, hairpinsLeft = L.hairpins;
  int hairpinEvery = L.hairpins > 0 ? imax(2, 12 / L.hairpins) : 0;
  for (int guard = 0; guard < 500; guard++) {
    int room = L.segs - reserve - segCount;
    if (room < 16) break;
    int enter = 0, hold, leave = 0;
    float curve = 0;
    if (!lastStraight && rng.chance(L.straightChance)) {
      hold = rng.range(L.straightMin, L.straightMax + 1);
      lastStraight = true;
    } else {
      enter = rng.range(L.enterMin, L.enterMax + 1);
      hold  = rng.range(L.holdMin, L.holdMax + 1);
      leave = rng.range(L.leaveMin, L.leaveMax + 1);
      float dir = rng.chance(L.rightBias) ? 1.0f : -1.0f;
      curve = dir * rng.frange(L.curveMin, L.curveMax);
      corners++;
      if (hairpinsLeft > 0 && hairpinEvery > 0 && (corners % hairpinEvery) == 0) {
        curve = dir * L.hairpinCurve;
        hold = rng.range(16, 25);
        hairpinsLeft--;
      }
      lastStraight = false;
    }
    int total = enter + hold + leave;
    if (total > room) {                      // shrink the section to fit
      hold -= total - room;
      if (hold < 2) break;
      total = enter + hold + leave;
    }
    float hill = 0;
    if (rng.chance(L.hillChance)) {
      float y = lastY() / SEG_LEN;
      hill = rng.frange(L.hillMin, L.hillMax) * rng.sign();
      // in the last third, head back towards 0 so the closing stays gentle
      if (segCount > L.segs * 2 / 3 && hill * y > 0) hill = -hill;
      float maxH = L.slopeMax * total;
      hill = clampF(hill, -maxH, maxH);
      if (fabsf(y + hill) > L.yMax) hill = -hill;
      if (fabsf(y + hill) > L.yMax) hill = 0;
    }
    addRoad(enter, hold, leave, curve, hill);
  }
  // Closing sweeper: ease the elevation EXACTLY back to 0, then the flat
  // pre-grid straight that ends at the start line.
  int R = L.segs - L.preStraight - segCount;
  if (R < 1) R = 1;
  float sY = lastY();
  float cc = rng.frange(L.curveMin * 0.4f, L.curveMin * 0.8f) * (rng.chance(L.rightBias) ? 1.0f : -1.0f);
  for (int n = 0; n < R; n++)
    addSeg(cc * sinf((float)PI * (float)n / R), easeInOut(sY, 0, (float)(n + 1) / R));
  for (int n = 0; n < L.preStraight; n++) addSeg(0, 0);
  track_finish();
}

// ═══════════════════════════════════════════════════════════════
//  2. DECORATION HELPERS
// ═══════════════════════════════════════════════════════════════
static inline uint8_t hUnits(float h) {
  return (uint8_t)iclamp((int)(h / BUILDING_H_UNIT + 0.5f), 1, 255);
}

static inline void setGround(Segment& s, int side, int mat) {
  s.ground = side ? (uint8_t)((s.ground & 0x0F) | (mat << 4)) : (uint8_t)((s.ground & 0xF0) | (mat & 15));
}

static void markTunnel(int start, int len) {
  for (int i = start; i < start + len && i < segCount; i++) {
    if (i < 0) continue;
    Segment& s = segments[i];
    s.flags = (uint8_t)((s.flags & (SEGF_START | SEGF_SPARE)) | SEGF_TUNNEL);
    s.build[0] = s.build[1] = 0;
    s.style[0] = s.style[1] = 0;
    s.spr[0] = s.spr[1] = -1;
  }
}

// A wall/cliff on one side over [start, start+len), skipping tunnels
static void wallRun(Rng& rng, int start, int len, int side, int type, float hMin, float hMax, int groundMat) {
  int i = start, end = start + len;
  while (i < end) {
    int piece = rng.range(6, 16);
    uint8_t hu = hUnits(rng.frange(hMin, hMax));
    uint8_t st = bldStyle(type, rng.range(0, 16));
    for (int k = 0; k < piece && i < end; k++, i++) {
      Segment& s = segAt(i);
      if (s.isTunnel()) continue;
      s.build[side] = hu;
      s.style[side] = st;
      s.spr[side] = -1;
      s.flags &= (uint8_t)~(side ? SEGF_RAIL_R : SEGF_RAIL_L);   // the wall replaces a rail
      if (groundMat >= 0) setGround(s, side, groundMat);
    }
  }
}

static bool nearStart(int i, int before, int after) {
  return i < after || i >= segCount - before;
}

// ═══════════════════════════════════════════════════════════════
//  NEON CITY: buildings both sides, sidewalks, cross streets with
//  zebra crossings, street lamps, one underpass
// ═══════════════════════════════════════════════════════════════
static void decorateCity(Rng& rng) {
  const int N = segCount;
  for (int i = 0; i < N; i++) segments[i].ground = groundPack(MAT_SIDEWALK, MAT_SIDEWALK);
  markTunnel((int)(N * 0.46f), 44);

  // Cross streets: a gap on both sides with a zebra crossing in the middle
  for (int i = rng.range(28, 44); i < N - 12;) {
    const int len = 9;
    bool clear = true;
    for (int k = -3; k < len + 3; k++) if (segAt(i + k).isTunnel()) clear = false;
    if (clear) {
      for (int k = 0; k < len; k++) segments[i + k].flags |= SEGF_SPARE;   // gap marker
      if (!nearStart(i, 60, 30))
        for (int k = 3; k < 6; k++) segments[i + k].flags |= SEGF_CROSSWALK;
    }
    i += len + rng.range(32, 70);
  }

  static const uint8_t TYPES[8] = { BLD_OFFICE, BLD_GLASS, BLD_BRICK, BLD_STONE,
                                    BLD_MODERN, BLD_SHOPS, BLD_BRICK, BLD_OFFICE };
  for (int side = 0; side < 2; side++) {
    int i = 0;
    while (i < N) {
      const Segment& s0 = segments[i];
      if (s0.isTunnel() || (s0.flags & SEGF_SPARE)) { i++; continue; }
      if (rng.chance(0.07f)) {               // small plaza on this side only
        int g = rng.range(3, 7);
        if (i + g / 2 < N && !segments[i + g / 2].isTunnel()) setSprite(i + g / 2, side, SPR_TREE, 1.30f);
        i += g;
        continue;
      }
      int type = TYPES[rng.range(0, 8)];
      int floors;
      switch (type) {
        case BLD_SHOPS:  floors = rng.range(2, 5); break;
        case BLD_BRICK:  floors = rng.range(5, 11); break;
        case BLD_STONE:  floors = rng.range(4, 9); break;
        case BLD_OFFICE: floors = rng.range(8, 19); break;
        default:         floors = rng.range(14, 31); break;   // glass, modern
      }
      uint8_t hu = hUnits((floors + 0.4f) * BUILDING_FLOOR_H);
      uint8_t st = bldStyle(type, rng.range(0, 16));
      int len = rng.range(5, 15);
      for (int k = 0; k < len && i < N; k++, i++) {
        Segment& t = segments[i];
        if (t.isTunnel() || (t.flags & SEGF_SPARE)) break;
        t.build[side] = hu;
        t.style[side] = st;
      }
    }
  }

  // Street lamps every 10 segments, alternating sides
  int lampSide = 0;
  for (int i = 4; i < N; i += 10) {
    Segment& s = segments[i];
    if (s.isTunnel() || (s.flags & SEGF_CROSSWALK)) continue;
    s.flags |= lampSide ? SEGF_LAMP_R : SEGF_LAMP_L;
    lampSide ^= 1;
  }

  // Street trees at the corners of the cross streets
  for (int i = 0; i < N; i++) {
    if (!(segments[i].flags & SEGF_SPARE)) continue;
    if (i > 0 && !(segments[i - 1].flags & SEGF_SPARE)) {
      setSprite(i + 1, 0, SPR_TREE, 1.32f);
      setSprite(i + 1, 1, SPR_TREE, 1.32f);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  SUNSET COAST: beach and sea on the left (sea walls with rails),
//  grass, palms, villas and rocky headlands on the right, one tunnel
// ═══════════════════════════════════════════════════════════════
static void decorateCoast(Rng& rng) {
  const int N = segCount;
  for (int i = 0; i < N; i++) segments[i].ground = groundPack(MAT_SAND, MAT_GRASS);

  // Sea walls: the water comes right up to the road, behind a rail
  for (int i = rng.range(60, 120); i < N - 80;) {
    int len = rng.range(40, 90);
    for (int k = 0; k < len && i + k < N - 60; k++) {
      Segment& s = segments[i + k];
      setGround(s, 0, MAT_WATER);
      s.flags |= SEGF_RAIL_L;
    }
    i += len + rng.range(80, 220);
  }

  // Headlands: rock cuttings on the right (a short one on both sides)
  int tunnelAt = (int)(N * 0.56f);
  for (int h = 0; h < 3; h++) {
    int start = (int)(N * (0.18f + 0.26f * h)) + rng.range(-20, 20);
    if (start > tunnelAt - 80 && start < tunnelAt + 80) start += 120;
    int len = rng.range(30, 60);
    wallRun(rng, start, len, 1, BLD_CLIFF, 26000.0f, 70000.0f, MAT_ROCK);
    if (h == 1) wallRun(rng, start + len / 3, 16, 0, BLD_CLIFF, 20000.0f, 40000.0f, MAT_ROCK);
  }
  // Tunnel through a headland, with cliffs on both sides at its mouths
  markTunnel(tunnelAt, 52);
  wallRun(rng, tunnelAt - 16, 16, 0, BLD_CLIFF, 30000.0f, 60000.0f, MAT_ROCK);
  wallRun(rng, tunnelAt - 16, 16, 1, BLD_CLIFF, 40000.0f, 80000.0f, MAT_ROCK);
  wallRun(rng, tunnelAt + 52, 16, 0, BLD_CLIFF, 30000.0f, 60000.0f, MAT_ROCK);
  wallRun(rng, tunnelAt + 52, 16, 1, BLD_CLIFF, 40000.0f, 80000.0f, MAT_ROCK);

  // Villages: white villas on the right
  for (int v = 0; v < 4; v++) {
    int i = (int)(N * (0.08f + 0.23f * v)) + rng.range(0, 30);
    int end = i + rng.range(30, 70);
    while (i < end && i < N - 50) {
      int len = rng.range(5, 9);
      uint8_t hu = hUnits(rng.range(2, 4) * BUILDING_FLOOR_H + 3000.0f);
      uint8_t st = bldStyle(BLD_VILLA, rng.range(0, 16));
      bool ok = true;
      for (int k = 0; k < len; k++) {
        const Segment& s = segAt(i + k);
        if (s.isTunnel() || s.build[1]) ok = false;
      }
      if (ok)
        for (int k = 0; k < len; k++) { segAt(i + k).build[1] = hu; segAt(i + k).style[1] = st; }
      i += len + rng.range(2, 6);
    }
  }

  // Sprites: palms and bushes on the right, palms on the beach, posts
  for (int i = 0; i < N; i++) {
    Segment& s = segments[i];
    if (s.isTunnel()) continue;
    if (!s.build[1] && (i % 7) == 3) {
      int r = rng.range(0, 10);
      if (r < 6) setSprite(i, 1, SPR_PALM, rng.frange(1.6f, 3.2f));
      else if (r < 8) setSprite(i, 1, SPR_BUSH, rng.frange(1.4f, 2.4f));
      else setSprite(i, 1, SPR_ROCK, rng.frange(1.6f, 2.8f));
    } else if (!s.build[1] && (i % 12) == 0 && s.spr[1] < 0) {
      setSprite(i, 1, SPR_POST, 1.12f);
    }
    if (!s.build[0] && s.mat(0) == MAT_SAND && (i % 19) == 5 && rng.chance(0.6f))
      setSprite(i, 0, SPR_PALM, rng.frange(1.7f, 2.3f));
  }
}

// ═══════════════════════════════════════════════════════════════
//  ALPINE PASS: rock walls on one side, guard rails over the drop on
//  the other, pines (snowy higher up), snow verges, two tunnels
// ═══════════════════════════════════════════════════════════════
static void decorateMountain(Rng& rng) {
  const int N = segCount;
  float minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i < N; i++) { minY = fminF(minY, segments[i].y); maxY = fmaxF(maxY, segments[i].y); }
  const float snowLine = minY + 0.55f * (maxY - minY);
  for (int i = 0; i < N; i++) {
    int m = segments[i].y > snowLine ? MAT_SNOW : MAT_GRASS;
    segments[i].ground = groundPack(m, m);
  }

  // Wall stretches alternate sides; meadows in between
  int side = 1;
  for (int i = 30; i < N - 60;) {
    int len = rng.range(70, 180);
    if (i + len > N - 60) len = N - 60 - i;
    if (rng.chance(0.72f)) {
      wallRun(rng, i, len, side, BLD_CLIFF, 40000.0f, 150000.0f, MAT_ROCK);
      for (int k = 0; k < len; k++) {
        Segment& s = segments[i + k];
        if (!s.isTunnel()) s.flags |= side ? SEGF_RAIL_L : SEGF_RAIL_R;
      }
      side ^= 1;
    }
    i += len + rng.range(10, 40);
  }

  // Two tunnels through the rock, cliffs on both sides at the mouths
  const int tA = (int)(N * 0.30f), lA = 42, tB = (int)(N * 0.70f), lB = 56;
  markTunnel(tA, lA);
  markTunnel(tB, lB);
  for (int t = 0; t < 2; t++) {
    int at = t ? tB : tA, len = t ? lB : lA;
    for (int s = 0; s < 2; s++) {
      wallRun(rng, at - 12, 12, s, BLD_CLIFF, 60000.0f, 120000.0f, MAT_ROCK);
      wallRun(rng, at + len, 12, s, BLD_CLIFF, 60000.0f, 120000.0f, MAT_ROCK);
    }
    for (int k = -12; k < len + 12; k++) segAt(at + k).flags &= (uint8_t)~(SEGF_RAIL_L | SEGF_RAIL_R);
  }

  // Forest: pines on open sides (behind the rail on the drop side)
  for (int i = 0; i < N; i++) {
    Segment& s = segments[i];
    if (s.isTunnel()) continue;
    for (int sd = 0; sd < 2; sd++) {
      if (s.build[sd] || s.spr[sd] >= 0) continue;
      uint32_t h = hash32((uint32_t)i * 2u + (uint32_t)sd);
      if ((h % 5) == 0) {
        bool snow = s.y > snowLine;
        int type = snow ? SPR_PINE_SNOW : SPR_PINE;
        if ((h >> 8) % 7 == 0) type = SPR_ROCK;
        setSprite(i, sd, type, rng.frange(1.55f, 4.0f));
      } else if ((i % 9) == 0 && !(s.flags & (sd ? SEGF_RAIL_R : SEGF_RAIL_L))) {
        setSprite(i, sd, SPR_POST, 1.12f);
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  3. MINIMAP
//  Heading integrated from the curve data; the total turn is normalised
//  to +-2 pi (mostly on the corners, a little everywhere), the residual
//  closure error is removed linearly and the outline is fitted into
//  0..255 keeping its aspect ratio.
// ═══════════════════════════════════════════════════════════════
static void buildMinimap() {
  const int N = segCount;
  if (N < MINIMAP_PTS) {
    memset(minimapX, 128, sizeof(minimapX));
    memset(minimapY, 128, sizeof(minimapY));
    return;
  }
  float H = 0, A = 0;
  for (int i = 0; i < N; i++) { H += segments[i].curve; A += fabsf(segments[i].curve); }
  const float K = 1.0f / SEG_LEN;                // heading change per unit of curve
  float total = H * K;
  float target = (total >= 0 ? 2.0f : -2.0f) * (float)PI;
  float diff = target - total;
  float kProp = (A > 1e-3f) ? 0.7f * diff / A : 0.0f;
  float kUni  = (A > 1e-3f) ? 0.3f * diff / N : diff / N;

  static float pts[MINIMAP_PTS][2];
  float h = 0, x = 0, y = 0;
  int j = 0;
  for (int i = 0; i < N; i++) {
    while (j < MINIMAP_PTS && (int)((long)j * N / MINIMAP_PTS) == i) { pts[j][0] = x; pts[j][1] = y; j++; }
    float c = segments[i].curve;
    h += c * K + fabsf(c) * kProp + kUni;
    x += sinf(h);
    y -= cosf(h);
  }
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (j = 0; j < MINIMAP_PTS; j++) {
    float f = (float)((long)j * N / MINIMAP_PTS) / N;
    pts[j][0] -= x * f;
    pts[j][1] -= y * f;
    minx = fminF(minx, pts[j][0]); maxx = fmaxF(maxx, pts[j][0]);
    miny = fminF(miny, pts[j][1]); maxy = fmaxF(maxy, pts[j][1]);
  }
  float w = maxx - minx, hh = maxy - miny;
  float span = fmaxF(fmaxF(w, hh), 1e-3f);
  float s = 255.0f / span;
  float ox = (255.0f - w * s) * 0.5f, oy = (255.0f - hh * s) * 0.5f;
  for (j = 0; j < MINIMAP_PTS; j++) {
    minimapX[j] = (uint8_t)iclamp((int)lroundf(ox + (pts[j][0] - minx) * s), 0, 255);
    minimapY[j] = (uint8_t)iclamp((int)lroundf(oy + (pts[j][1] - miny) * s), 0, 255);
  }
}

void minimapAt(float z, float& u, float& v) {
  float f = z / trackLength * MINIMAP_PTS;
  f = fmodf(f, (float)MINIMAP_PTS);
  if (f < 0) f += MINIMAP_PTS;
  int j = (int)f;
  if (j < 0 || j >= MINIMAP_PTS) j = 0;
  float t = f - (float)j;
  int k = (j + 1) % MINIMAP_PTS;
  u = (minimapX[j] + (minimapX[k] - minimapX[j]) * t) * (1.0f / 255.0f);
  v = (minimapY[j] + (minimapY[k] - minimapY[j]) * t) * (1.0f / 255.0f);
}

// ═══════════════════════════════════════════════════════════════
//  BUILD
// ═══════════════════════════════════════════════════════════════
void buildCircuit(int id) {
  id = iclamp(id, 0, NUM_CIRCUITS - 1);
  currentCircuit = id;
  const Layout& L = LAYOUTS[id];
  const CircuitInfo& info = CIRCUITS[id];
  Rng rng(L.seed);
  genLayout(L, rng);
  if (segCount <= 0) { plat_log("circuit: no segment buffer\n"); return; }

  switch (info.theme) {
    case THEME_COAST:    decorateCoast(rng); break;
    case THEME_MOUNTAIN: decorateMountain(rng); break;
    default:             decorateCity(rng); break;
  }
  // Start/finish line; nothing in the way on the grid
  segments[0].flags |= SEGF_START;
  for (int i = 0; i < segCount; i++) {
    Segment& s = segments[i];
    s.flags &= (uint8_t)~SEGF_SPARE;
    if (nearStart(i, 50, 6)) s.flags &= (uint8_t)~SEGF_CROSSWALK;
    if (s.isTunnel()) s.flags &= (uint8_t)~(SEGF_LAMP_L | SEGF_LAMP_R | SEGF_RAIL_L | SEGF_RAIL_R | SEGF_CROSSWALK);
  }

  buildMinimap();
  setTheme(info.theme);
  initBackground(info.theme);
  setDayCycle(info.cycleStart);

  int bl = 0, br = 0, tun = 0, rails = 0, cw = 0, lamps = 0, spr = 0;
  float minY = 0, maxY = 0;
  for (int i = 0; i < segCount; i++) {
    const Segment& s = segments[i];
    bl += s.build[0] != 0;
    br += s.build[1] != 0;
    tun += s.isTunnel();
    rails += (s.flags & (SEGF_RAIL_L | SEGF_RAIL_R)) != 0;
    cw += (s.flags & SEGF_CROSSWALK) != 0;
    lamps += (s.flags & (SEGF_LAMP_L | SEGF_LAMP_R)) != 0;
    spr += (s.spr[0] >= 0) + (s.spr[1] >= 0);
    minY = fminF(minY, s.y);
    maxY = fmaxF(maxY, s.y);
  }
  plat_log("circuit %d %s: %d segments, buildings L%d%% R%d%%, tunnel %d, rails %d, "
           "crosswalks %d, lamps %d, sprites %d, elevation %.0f..%.0f\n",
           id, info.name, segCount, bl * 100 / segCount, br * 100 / segCount, tun, rails, cw, lamps, spr,
           minY, maxY);
}
