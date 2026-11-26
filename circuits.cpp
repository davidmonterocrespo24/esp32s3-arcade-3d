/*
  ═══════════════════════════════════════════════════════════════
  CIRCUITS — implementation

  1. LAYOUT. Every circuit is an explicit list of sections: straights
     and corners, a corner given by its turn angle and length. The curve
     value of a corner follows from its angle (heading change per segment
     = curve / SEG_LEN), with a linear curvature ramp at both ends (a
     clothoid, like real roads). The angles add up to exactly 360 degrees
     and two straights (flagged A and B, not parallel) are stretched so the
     plan closes on itself: the minimap is the true shape of the circuit.
  2. ELEVATION. Key heights at section ends (Y_UNIT world units), eased
     with a cosine in between: keys are the crests and the dips, sections
     without a key keep climbing / falling smoothly.
  3. DECORATION. Each section names a zone (a city district, a beach, a
     cliff road ...). Zone painters set ground materials, buildings,
     rails, lamps, crossings and sprites from a seeded Rng; generic passes
     then dress the tunnel mouths, put chevrons on the outside of sharp
     corners and distance boards before the hardest ones. Every sprite goes
     through placeSprite(), which enforces the keep-out rules (nothing on
     the road, in tunnels, through a facade, a rail or a lamp, nothing on
     the grid, land props only on land).
  4. MINIMAP from the curve data (already closed, so the normalisation is
     only a safety net).
  Deterministic: a circuit is identical every time it is built.
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

// Taglines stay under 27 characters (the select screen draws the minimap
// preview right of them).
const CircuitInfo CIRCUITS[NUM_CIRCUITS] = {
  { "NEON CITY",    "DOWNTOWN STREETS AT NIGHT",  THEME_CITY,     3, 1.40f, 2.40f },
  { "SUNSET COAST", "SEASIDE HIGHWAY AT SUNSET",  THEME_COAST,    3, 0.50f, 1.40f },
  { "ALPINE PASS",  "HAIRPINS, SNOW AND TUNNELS", THEME_MOUNTAIN, 3, 3.30f, 4.30f },
};

int currentCircuit = 0;
uint8_t minimapX[MINIMAP_PTS], minimapY[MINIMAP_PTS];

const CircuitInfo& curCircuit() {
  return CIRCUITS[iclamp(currentCircuit, 0, NUM_CIRCUITS - 1)];
}

// ═══════════════════════════════════════════════════════════════
//  1. LAYOUT DESCRIPTION
// ═══════════════════════════════════════════════════════════════
#define Y_FREE (-128)                 // no elevation key at the end of the section
static const float Y_UNIT = 100.0f;  // Sec::y unit (world units)

enum SecFlag : uint8_t {
  SF_TUNNEL    = 0x01,                // the whole section is a tunnel
  SF_STRETCH_A = 0x02,                // the two straights stretched to close the loop
  SF_STRETCH_B = 0x04,
};

struct Sec {
  int16_t len;     // segments
  int16_t turn;    // heading change in degrees, + = right, 0 = straight
  uint8_t ease;    // curvature ramp at each end of a corner (segments)
  int8_t  y;       // elevation key at the end of the section (Y_UNIT), or Y_FREE
  uint8_t zone;    // decoration zone of the theme (enums below)
  uint8_t flags;   // SecFlag
};

#define ST(len, y, zone)               { (int16_t)(len), 0, 0, (int8_t)(y), (uint8_t)(zone), 0 }
#define STF(len, y, zone, f)           { (int16_t)(len), 0, 0, (int8_t)(y), (uint8_t)(zone), (uint8_t)(f) }
#define CO(turn, len, ease, y, zone)   { (int16_t)(len), (int16_t)(turn), (uint8_t)(ease), (int8_t)(y), (uint8_t)(zone), 0 }
#define COF(turn, len, ease, y, zone, f) { (int16_t)(len), (int16_t)(turn), (uint8_t)(ease), (int8_t)(y), (uint8_t)(zone), (uint8_t)(f) }

// ---------------------------------------------------------------
//  NEON CITY: a downtown street circuit. Clockwise, the start on the
//  west side heading north. Grid-like 90-degree corners between short
//  straights, a jog through the financial district, the harbour front
//  (water on the left) ending in a hairpin, an underpass, a chicane.
// ---------------------------------------------------------------
enum CityZone : uint8_t {
  CZ_BOULEVARD = 0,   // start / finish boulevard: stone and offices, trees, flags
  CZ_FINANCE,         // glass and dark towers, planters
  CZ_OLDTOWN,         // brick and sandstone, shops at street level, trees
  CZ_SHOPPING,        // shopfronts, neon signs, billboards
  CZ_PARK,            // a park on the right, buildings on the left
  CZ_HARBOUR,         // water and a rail on the left, warehouses on the right
  CZ_UNDERPASS,       // retaining walls down into the underpass (tunnel)
};

static const Sec CITY_SECS[] = {
  ST(  40, 0, CZ_BOULEVARD),                       //  0 after the line
  CO(  90, 52, 8, 2, CZ_BOULEVARD),                //  1 T1 -> east
  STF(120, 6, CZ_FINANCE, SF_STRETCH_A),           //  2 the avenue, climbing
  CO( -90, 50, 8, 7, CZ_FINANCE),                  //  3 T2 -> north
  ST(  86, 8, CZ_FINANCE),                         //  4
  CO(  90, 50, 8, Y_FREE, CZ_OLDTOWN),             //  5 T3 -> east
  ST( 120, 4, CZ_OLDTOWN),                         //  6
  CO(  90, 52, 8, Y_FREE, CZ_OLDTOWN),             //  7 T4 -> south
  ST(  60, 2, CZ_PARK),                            //  8 park on the right
  CO( -90, 50, 8, Y_FREE, CZ_PARK),                //  9 T5 -> east
  ST(  70, 0, CZ_SHOPPING),                        // 10
  CO(  90, 52, 8, Y_FREE, CZ_HARBOUR),             // 11 T6 -> south
  STF(180, -2, CZ_HARBOUR, SF_STRETCH_B),          // 12 harbour front
  CO( 180, 84, 10, -2, CZ_SHOPPING),               // 13 hairpin round the end of the block -> north
  ST( 116, 0, CZ_SHOPPING),                        // 14
  CO( -90, 50, 8, Y_FREE, CZ_SHOPPING),            // 15 T8 -> west
  ST(  18, -2, CZ_UNDERPASS),                      // 16 ramp down
  STF( 56, -6, CZ_UNDERPASS, SF_TUNNEL),           // 17 underpass
  ST(  40, 0, CZ_UNDERPASS),                       // 18 ramp up
  CO( -90, 50, 8, Y_FREE, CZ_OLDTOWN),             // 19 T9 -> south
  ST(  80, 2, CZ_OLDTOWN),                         // 20
  CO(  90, 52, 8, Y_FREE, CZ_FINANCE),             // 21 T10 -> west
  ST(  76, 3, CZ_FINANCE),                         // 22
  CO( -30, 28, 6, Y_FREE, CZ_BOULEVARD),           // 23 chicane
  CO(  30, 28, 6, Y_FREE, CZ_BOULEVARD),           // 24
  ST(  80, 0, CZ_BOULEVARD),                       // 25
  CO(  90, 52, 8, 0, CZ_BOULEVARD),                // 26 T11 -> north
  ST(  80, 0, CZ_BOULEVARD),                       // 27 pre-grid straight
};

// ---------------------------------------------------------------
//  SUNSET COAST: clockwise around a headland, so the sea is always on
//  the left. Long sweepers: beaches, a sea-wall cliff road, a cape with
//  a lighthouse and a tunnel through the headland, a white village in a
//  bay, rolling hills inland.
// ---------------------------------------------------------------
enum CoastZone : uint8_t {
  KZ_TOWN = 0,        // promenade: beach + umbrellas left, villas right, lamps
  KZ_BEACH,           // sand left, palms / grass right
  KZ_SEAWALL,         // sea wall + rail left, rock cutting right
  KZ_CAPE,            // rocks both sides, cliffs right, boulders
  KZ_LIGHTHOUSE,      // the cape with the lighthouse on the left
  KZ_VILLAGE,         // beach left, villas and cypresses right
  KZ_HILLS,           // inland: grass, flowers, fences, cypresses
  KZ_TUNNEL,          // through the headland (cliffs at the mouths)
};

static const Sec COAST_SECS[] = {
  ST(  40, 0, KZ_TOWN),                            //  0 promenade after the line
  CO(  45, 80, 16, 4, KZ_TOWN),                    //  1 -> north-east
  ST(  70, 8, KZ_BEACH),                           //  2
  CO( -30, 70, 14, Y_FREE, KZ_BEACH),              //  3 a bay
  ST(  70, 14, KZ_BEACH),                          //  4
  CO(  75, 120, 18, 22, KZ_SEAWALL),               //  5 round the cape on the sea wall -> east
  ST(  30, 26, KZ_CAPE),                           //  6
  CO(  40, 80, 14, 28, KZ_LIGHTHOUSE),             //  7 lighthouse point -> south-east
  STF( 64, 22, KZ_TUNNEL, SF_TUNNEL),              //  8 tunnel through the headland
  CO( -50, 90, 16, 14, KZ_VILLAGE),                //  9 the village bay -> east
  ST(  70, 10, KZ_VILLAGE),                        // 10
  CO( 100, 140, 18, Y_FREE, KZ_BEACH),             // 11 -> south
  STF(100, 18, KZ_HILLS, SF_STRETCH_B),            // 12 inland over the hills
  CO( -40, 60, 14, Y_FREE, KZ_HILLS),              // 13 S-bend
  CO(  40, 60, 14, 10, KZ_HILLS),                  // 14
  CO(  90, 130, 18, Y_FREE, KZ_SEAWALL),           // 15 back to the sea -> west
  STF(100, 4, KZ_SEAWALL, SF_STRETCH_A),           // 16 the sea-wall straight
  CO( -25, 70, 12, Y_FREE, KZ_BEACH),              // 17
  ST( 110, 8, KZ_BEACH),                           // 18
  CO(  25, 70, 12, Y_FREE, KZ_BEACH),              // 19
  ST(  90, 3, KZ_BEACH),                           // 20
  CO(  90, 120, 18, 0, KZ_TOWN),                   // 21 into town -> north
  ST(  80, 0, KZ_TOWN),                            // 22 pre-grid straight
};

// ---------------------------------------------------------------
//  ALPINE PASS: from a valley meadow up a rock face with two switchback
//  hairpins, through a tunnel onto the snowy high road past a lake, then
//  down through a second tunnel and the forest. Rock wall on the uphill
//  side, a rail over the drop on the other.
// ---------------------------------------------------------------
enum MountainZone : uint8_t {
  MZ_VALLEY = 0,      // meadows, fences, flowers, chalets
  MZ_FOREST,          // dense pines both sides
  MZ_WALL_L,          // rock wall left, drop + rail right
  MZ_WALL_R,          // rock wall right, drop + rail left
  MZ_HIGH,            // above the tree line: snow, snowy pines, poles, boulders
  MZ_LAKE,            // a lake on the left behind a rail
  MZ_TUNNEL,          // through the rock (cliffs at the mouths)
};

// The uphill side of the switchbacks is north: heading west it is on the
// right, heading east on the left, and it is the inside of each hairpin.
static const Sec MOUNTAIN_SECS[] = {
  ST(  40, 0, MZ_VALLEY),                          //  0 after the line
  CO(  30, 50, 10, 2, MZ_VALLEY),                  //  1
  ST(  50, 6, MZ_FOREST),                          //  2
  CO( -30, 50, 10, Y_FREE, MZ_FOREST),             //  3
  ST(  30, 14, MZ_WALL_R),                         //  4 the climb starts
  CO( -75, 60, 10, 18, MZ_WALL_R),                 //  5 -> west
  ST(  60, 24, MZ_WALL_R),                         //  6
  CO( 180, 76, 12, 32, MZ_WALL_R),                 //  7 hairpin right -> east
  ST(  70, 40, MZ_WALL_L),                         //  8
  CO(-180, 76, 12, 48, MZ_WALL_L),                 //  9 hairpin left -> west
  ST(  60, 56, MZ_WALL_R),                         // 10
  CO( 120, 80, 10, 62, MZ_HIGH),                   // 11 -> north-east
  STF( 64, 68, MZ_TUNNEL, SF_TUNNEL),              // 12 tunnel 1
  CO(  45, 60, 10, 72, MZ_HIGH),                   // 13 -> east
  ST( 100, 76, MZ_HIGH),                           // 14 the high road
  CO(  45, 60, 10, Y_FREE, MZ_LAKE),               // 15
  ST(  80, 74, MZ_LAKE),                           // 16 along the lake
  CO( -30, 50, 10, Y_FREE, MZ_HIGH),               // 17
  CO(  75, 80, 12, 64, MZ_WALL_R),                 // 18 -> south
  STF( 70, 52, MZ_WALL_R, SF_STRETCH_B),           // 19 the descent
  STF( 56, 40, MZ_TUNNEL, SF_TUNNEL),              // 20 tunnel 2
  CO(  60, 80, 12, 30, MZ_FOREST),                 // 21 -> west-south-west
  STF( 90, 22, MZ_FOREST, SF_STRETCH_A),           // 22 down through the forest
  CO( -15, 40, 8, Y_FREE, MZ_FOREST),              // 23 a kink
  CO(  15, 40, 8, 12, MZ_FOREST),                  // 24
  ST(  60, 8, MZ_VALLEY),                          // 25
  CO( -20, 46, 8, Y_FREE, MZ_VALLEY),              // 26
  CO( 140, 110, 14, 0, MZ_VALLEY),                 // 27 -> north
  ST(  80, 0, MZ_VALLEY),                          // 28 pre-grid straight
};

struct Design { const Sec* secs; int count; uint32_t seed; };
#define DESIGN(a, seed) { a, (int)(sizeof(a) / sizeof(a[0])), seed }
static const Design DESIGNS[NUM_CIRCUITS] = {
  DESIGN(CITY_SECS, 0xC17E0001u),
  DESIGN(COAST_SECS, 0xC0A57002u),
  DESIGN(MOUNTAIN_SECS, 0xA1B50003u),
};

// ═══════════════════════════════════════════════════════════════
//  1b. LAYOUT GENERATION
// ═══════════════════════════════════════════════════════════════
#define MAX_SECS 48
// Build-time working data (only touched while a circuit is built): in PSRAM,
// allocated once on the first build, so it costs no internal RAM.
struct BuildScratch {
  Sec   sec[MAX_SECS];                 // working copy (the closure stretches two straights)
  int   secStart[MAX_SECS + 1];        // first segment of each section (+ end)
  float head[MAX_SECS];                // plan heading at each section start
  float pts[MINIMAP_PTS][2];           // minimap outline before fitting
};
static BuildScratch* sB = nullptr;
static int sSecN = 0;
#define sSec      (sB->sec)
#define sSecStart (sB->secStart)

static bool scratchInit() {
  if (sB) return true;
  bool psram = true;
  sB = (BuildScratch*)plat_alloc_psram(sizeof(BuildScratch));
  if (!sB) { psram = false; sB = (BuildScratch*)plat_alloc_internal(sizeof(BuildScratch)); }
  if (!sB) { plat_log("circuit: no memory for the build scratch\n"); return false; }
  plat_log("circuit: build scratch (%u B) in %s\n", (unsigned)sizeof(BuildScratch), psram ? "PSRAM" : "internal SRAM");
  return true;
}

// Curvature weight of segment n of a corner: linear ramps over `ease`
// segments at both ends. The weights of a whole corner add up to len - ease.
static inline float secWeight(const Sec& s, int n) {
  const int e = imin(s.ease, s.len / 2);
  if (e > 0 && n < e) return (n + 0.5f) / e;
  if (e > 0 && n >= s.len - e) return (s.len - n - 0.5f) / e;
  return 1.0f;
}

// Peak curve value of a section: turn (radians) * SEG_LEN / sum of weights
static float secPeak(const Sec& s) {
  if (!s.turn || s.len <= 0) return 0;
  const int e = imin(s.ease, s.len / 2);
  const float wsum = (float)(s.len - e);
  if (wsum <= 0.5f) return 0;
  return (float)s.turn * ((float)PI / 180.0f) * SEG_LEN / wsum;
}

// Integrate the plan of the section list (segment units, heading 0 = up,
// + = right: the minimap's convention). head[k] = heading at section k.
static void integratePlan(float& ex, float& ey, float* head) {
  float h = 0, x = 0, y = 0;
  for (int k = 0; k < sSecN; k++) {
    const Sec& s = sSec[k];
    if (head) head[k] = h;
    const float peak = secPeak(s) * (1.0f / SEG_LEN);
    for (int n = 0; n < s.len; n++) {
      h += peak * secWeight(s, n);
      x += sinf(h);
      y -= cosf(h);
    }
  }
  ex = x;
  ey = y;
}

// Stretch straights A and B so the loop closes: dA * uA + dB * uB = -end
static void closeLoop() {
  float* head = sB->head;
  float ex, ey;
  integratePlan(ex, ey, head);
  int a = -1, b = -1;
  for (int k = 0; k < sSecN; k++) {
    if (sSec[k].flags & SF_STRETCH_A) a = k;
    if (sSec[k].flags & SF_STRETCH_B) b = k;
  }
  if (a < 0 || b < 0 || sSec[a].turn || sSec[b].turn) return;
  const float ax = sinf(head[a]), ay = -cosf(head[a]);
  const float bx = sinf(head[b]), by = -cosf(head[b]);
  const float det = ax * by - ay * bx;
  if (fabsf(det) < 0.3f) return;                 // (nearly) parallel: cannot solve
  const float da = (-ex * by + ey * bx) / det;
  const float db = (-ax * ey + ay * ex) / det;
  sSec[a].len = (int16_t)imax(10, sSec[a].len + (int)lroundf(da));
  sSec[b].len = (int16_t)imax(10, sSec[b].len + (int)lroundf(db));
}

static void genLayout(const Design& D) {
  sSecN = imin(D.count, MAX_SECS);
  memcpy(sSec, D.secs, sizeof(Sec) * (size_t)sSecN);
  closeLoop();

  track_clear();
  for (int k = 0; k < sSecN; k++) {
    sSecStart[k] = segCount;
    const float peak = secPeak(sSec[k]);
    for (int n = 0; n < sSec[k].len; n++)
      if (addSeg(peak * secWeight(sSec[k], n), 0) < 0) break;
  }
  sSecStart[sSecN] = segCount;

  // Elevation: cosine between keys. Boundary b is the far end of segment
  // b - 1; boundary 0 (the start line) and boundary segCount are level 0.
  int b0 = 0;
  float y0 = 0;
  for (int k = 0; k <= sSecN; k++) {
    int b1;
    float y1;
    if (k < sSecN) {
      if (sSec[k].y == Y_FREE) continue;
      b1 = sSecStart[k + 1];
      y1 = sSec[k].y * Y_UNIT;
    } else {
      b1 = segCount;
      y1 = 0;
    }
    if (b1 <= b0) continue;
    for (int b = b0 + 1; b <= b1; b++) {
      float t = (float)(b - b0) / (float)(b1 - b0);
      segments[b - 1].y = y0 + (y1 - y0) * (0.5f - 0.5f * cosf(t * (float)PI));
    }
    b0 = b1;
    y0 = y1;
  }
  track_finish();
}

// Section containing segment i
static int secOf(int i) {
  i = segWrap(i);
  int lo = 0, hi = sSecN - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) >> 1;
    if (sSecStart[mid] <= i) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}
static inline int zoneAt(int i) { return sSec[secOf(i)].zone; }

// ═══════════════════════════════════════════════════════════════
//  2. DECORATION HELPERS
// ═══════════════════════════════════════════════════════════════
static Rng sRng;

static inline uint8_t railFlag(int side) { return side ? SEGF_RAIL_R : SEGF_RAIL_L; }
static inline uint8_t lampFlag(int side) { return side ? SEGF_LAMP_R : SEGF_LAMP_L; }

static inline void setMat(int i, int side, int mat) {
  Segment& s = segAt(i);
  s.ground = side ? (uint8_t)((s.ground & 0x0F) | ((mat & 15) << 4))
                  : (uint8_t)((s.ground & 0xF0) | (mat & 15));
}

static void setBuilding(int i, int side, int type, int h, int variant) {
  Segment& s = segAt(i);
  if (s.isTunnel()) return;
  s.build[side] = (uint8_t)iclamp(h, 1, 255);
  s.style[side] = bldStyle(type, variant);
  s.flags &= (uint8_t)~railFlag(side);          // the face replaces a rail
}

static void clearSide(int i, int side) {
  Segment& s = segAt(i);
  s.build[side] = 0;
  s.style[side] = 0;
  s.spr[side] = -1;
  s.sprOff[side] = 0;
}

static inline bool isWaterProp(int type) { return type == SPR_SAILBOAT; }

// Keep-out rules for a sprite at |x| = dist on one side of segment i.
static bool spriteFits(int i, int side, int type, float dist) {
  if (segCount <= 0) return false;
  i = segWrap(i);
  const Segment& s = segments[i];
  if (s.isTunnel() || segAt(i - 1).isTunnel()) return false;   // inside, or right at an exit
  if (s.spr[side] >= 0) return false;
  dist = (float)lroundf(dist * 16.0f) * (1.0f / 16.0f);         // as stored in sprOff
  const float half = spriteWidth(type) * 0.5f;
  const float inner = dist - half, outer = dist + half;
  if (inner < 1.12f) return false;                               // on the road or the rumble strip
  if (dist > 15.5f) return false;                                // sprOff range
  if (s.build[side] && outer > BUILDING_OFFSET - 0.02f) return false;   // through a facade
  if ((s.flags & railFlag(side)) && inner < RAIL_X + 0.12f) return false;
  if ((s.flags & lampFlag(side)) && inner < 1.62f) return false;
  if ((s.flags & SEGF_CROSSWALK) && inner < 1.62f && type != SPR_TRAFFIC_LIGHT) return false;
  if ((i <= 1 || i >= segCount - 2) && inner < 1.62f) return false;    // start gantry
  const int mat = s.mat(side);
  if (isWaterProp(type)) {
    if (mat == MAT_WATER) return inner > SEAWALL_X + 0.3f;
    if (mat == MAT_SAND) return inner > BEACH_WATER_X + 0.3f;
    return false;
  }
  if (mat == MAT_WATER) return false;
  if (mat == MAT_SAND && outer > BEACH_WATER_X - 0.05f) return false;
  return true;
}

static bool placeSprite(int i, int side, int type, float dist) {
  if (!spriteFits(i, side, type, dist)) return false;
  setSprite(segWrap(i), side, type, dist);
  return true;
}

// A street lamp (drawn by the road pass at |x| 1.28) unless the spot is in a
// tunnel, at its exit, or taken by a prop near the kerb.
static bool addLamp(int i, int side) {
  Segment& s = segAt(i);
  if (s.isTunnel() || segAt(i - 1).isTunnel() || segAt(i + 1).isTunnel()) return false;
  if (s.flags & (SEGF_CROSSWALK | SEGF_SPARE)) return false;
  if (s.spr[side] >= 0 && s.sprX(side) * (side ? 1.0f : -1.0f) - spriteWidth(s.spr[side]) * 0.5f < 1.62f)
    return false;
  s.flags |= lampFlag(side);
  return true;
}

// Signs that must be there (chevrons, boards): replace whatever prop was
// at that spot, still within the keep-out rules.
static bool forceSprite(int i, int side, int type, float dist) {
  Segment& s = segAt(i);
  int8_t old = s.spr[side];
  uint8_t oldOff = s.sprOff[side];
  s.spr[side] = -1;
  if (placeSprite(i, side, type, dist)) return true;
  s.spr[side] = old;
  s.sprOff[side] = oldOff;
  return false;
}

// A building of random length on one side starting at i; returns its length.
struct BldPick { uint8_t type, hMin, hMax, weight; };

static int pickWeighted(const BldPick* t, int n) {
  int total = 0;
  for (int k = 0; k < n; k++) total += t[k].weight;
  int r = sRng.range(0, imax(total, 1));
  for (int k = 0; k < n; k++) {
    if (r < t[k].weight) return k;
    r -= t[k].weight;
  }
  return 0;
}

// Mark the tunnel sections; clear everything inside.
static void markTunnels() {
  for (int k = 0; k < sSecN; k++) {
    if (!(sSec[k].flags & SF_TUNNEL)) continue;
    for (int i = sSecStart[k]; i < sSecStart[k + 1]; i++) {
      Segment& s = segments[i];
      s.flags = (uint8_t)((s.flags & SEGF_START) | SEGF_TUNNEL);
      clearSide(i, 0);
      clearSide(i, 1);
    }
  }
}

// Rock / concrete faces both sides at a tunnel's mouths
static void dressTunnelMouths(int type, int hMin, int hMax, int len) {
  for (int k = 0; k < sSecN; k++) {
    if (!(sSec[k].flags & SF_TUNNEL)) continue;
    const int t0 = sSecStart[k], t1 = sSecStart[k + 1];
    for (int side = 0; side < 2; side++) {
      for (int end = 0; end < 2; end++) {
        int h = sRng.range(hMin, hMax + 1);
        int v = sRng.range(0, 16);
        for (int j = 0; j < len; j++) {
          int i = end ? t1 + j : t0 - 1 - j;
          if (segAt(i).isTunnel()) break;
          // taper the face down away from the mouth
          int hh = h - (h - hMin) * j / imax(len, 1) / 2;
          clearSide(i, side);
          setBuilding(i, side, type, hh, v);
          segAt(i).flags &= (uint8_t)~(lampFlag(side) | railFlag(side));
        }
      }
    }
  }
}

// Chevrons on the outside of sharp corners, distance boards before the
// hardest ones. Run after the zones (they replace ordinary props).
static void cornerSigns(float chevronCurve, float boardCurve) {
  for (int k = 0; k < sSecN; k++) {
    const Sec& c = sSec[k];
    if (!c.turn || (c.flags & SF_TUNNEL)) continue;
    const float peak = fabsf(secPeak(c));
    if (peak < chevronCurve) continue;
    const int outside = c.turn > 0 ? 0 : 1;            // right turn: outside is the left
    const int type = c.turn > 0 ? SPR_CHEVRON_R : SPR_CHEVRON_L;
    const int s0 = sSecStart[k];
    const int last = s0 + (int)(c.len * 0.70f);
    // on the kerb in front of a facade, behind a rail, or just off the verge
    for (int i = s0 - 8; i <= last; i += 4) {
      const Segment& s = segAt(i);
      float d = s.build[outside] ? 1.30f : ((s.flags & railFlag(outside)) ? 1.50f : 1.32f);
      if (!forceSprite(i, outside, type, d)) forceSprite(i + 1, outside, type, d);
    }
    if (peak >= boardCurve) {
      static const int8_t BOARDS[3] = { SPR_BOARD_3, SPR_BOARD_2, SPR_BOARD_1 };
      for (int b = 0; b < 3; b++) {
        int i = s0 - 34 + b * 8;
        const Segment& s = segAt(i);
        int sd = (s.mat(outside) == MAT_WATER) ? 1 - outside : outside;   // never in the water
        float d = s.build[sd] ? 1.30f : ((s.flags & railFlag(sd)) ? 1.46f : 1.30f);
        if (!forceSprite(i, sd, BOARDS[b], d)) forceSprite(i + 1, sd, BOARDS[b], d);
      }
    }
  }
}

// Building runs along one side over [i0, i1): consecutive buildings never
// share their height, so every one shows its front face.
static int sLastH[2];                  // last building height per side (reset per build)

static void buildingRun(int i0, int i1, int side, const BldPick* table, int n, int lenMin, int lenMax) {
  int i = i0;
  while (i < i1) {
    const BldPick& p = table[pickWeighted(table, n)];
    int len = sRng.range(lenMin, lenMax + 1);
    if (p.type == BLD_SHOPS) len = imax(3, len * 2 / 3);
    int h = sRng.range(p.hMin, p.hMax + 1);
    const int dh = h - sLastH[side];
    if (dh > -12 && dh < 12) h = (h + 24 <= 255) ? h + 24 : h - 24;
    sLastH[side] = h;
    const int v = sRng.range(0, 16);
    for (int j = 0; j < len && i < i1; j++, i++) {
      const Segment& s = segAt(i);
      if (s.isTunnel() || (s.flags & SEGF_SPARE)) { i++; break; }
      setBuilding(i, side, p.type, h, v);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  3a. NEON CITY
// ═══════════════════════════════════════════════════════════════
static const BldPick CITY_BOULEVARD[] = {
  { BLD_STONE, 100, 160, 4 }, { BLD_OFFICE, 110, 180, 3 }, { BLD_GLASS, 160, 230, 1 }, { BLD_SHOPS, 50, 80, 2 } };
static const BldPick CITY_FINANCE[] = {
  { BLD_GLASS, 180, 255, 4 }, { BLD_MODERN, 170, 255, 3 }, { BLD_OFFICE, 120, 210, 3 } };
static const BldPick CITY_OLDTOWN[] = {
  { BLD_BRICK, 70, 130, 4 }, { BLD_STONE, 80, 140, 3 }, { BLD_SHOPS, 45, 75, 3 } };
static const BldPick CITY_SHOPPING[] = {
  { BLD_SHOPS, 40, 80, 5 }, { BLD_BRICK, 70, 120, 2 }, { BLD_MODERN, 150, 230, 2 }, { BLD_OFFICE, 90, 150, 1 } };
static const BldPick CITY_HARBOUR[] = {
  { BLD_BRICK, 60, 100, 4 }, { BLD_OFFICE, 80, 130, 2 }, { BLD_MODERN, 140, 220, 1 } };

static const BldPick* cityTable(int zone, int& n) {
  switch (zone) {
    case CZ_FINANCE:  n = 3; return CITY_FINANCE;
    case CZ_OLDTOWN:  n = 3; return CITY_OLDTOWN;
    case CZ_SHOPPING: n = 4; return CITY_SHOPPING;
    case CZ_HARBOUR:  n = 3; return CITY_HARBOUR;
    default:          n = 4; return CITY_BOULEVARD;
  }
}

static void decorateCity() {
  const int N = segCount;
  for (int i = 0; i < N; i++) segments[i].ground = groundPack(MAT_SIDEWALK, MAT_SIDEWALK);

  // Zone grounds: the harbour's water + rail, the park's lawn
  for (int k = 0; k < sSecN; k++) {
    for (int i = sSecStart[k]; i < sSecStart[k + 1]; i++) {
      if (sSec[k].zone == CZ_HARBOUR) { setMat(i, 0, MAT_WATER); segments[i].flags |= SEGF_RAIL_L; }
      if (sSec[k].zone == CZ_PARK) setMat(i, 1, MAT_GRASS);
    }
  }
  markTunnels();

  // Cross streets on the straights of the built-up districts: a gap on both
  // sides (SEGF_SPARE while building) with a zebra crossing in the middle.
  for (int k = 0; k < sSecN; k++) {
    const Sec& c = sSec[k];
    if (c.turn || (c.flags & SF_TUNNEL) || c.zone == CZ_HARBOUR || c.zone == CZ_UNDERPASS) continue;
    const int a = sSecStart[k] + 12, b = sSecStart[k + 1] - 12;
    for (int i = a + sRng.range(0, 14); i + 8 <= b; i += 8 + sRng.range(34, 60)) {
      if (i < 40 || i > N - 70) continue;                  // keep the start boulevard closed
      for (int j = 0; j < 8; j++) segments[i + j].flags |= SEGF_SPARE;
      segments[i + 3].flags |= SEGF_CROSSWALK;
      segments[i + 4].flags |= SEGF_CROSSWALK;
    }
  }

  // Buildings, district by district
  for (int side = 0; side < 2; side++) {
    for (int k = 0; k < sSecN; k++) {
      const int z = sSec[k].zone;
      if (sSec[k].flags & SF_TUNNEL) continue;
      if (z == CZ_UNDERPASS) {                             // retaining walls of the trench
        for (int i = sSecStart[k]; i < sSecStart[k + 1]; i++) setBuilding(i, side, BLD_WALL, 34, 0);
        continue;
      }
      if ((z == CZ_PARK && side == 1) || (z == CZ_HARBOUR && side == 0)) continue;
      int n;
      const BldPick* t = cityTable(z, n);
      // the inside of a sharp corner opens into a small plaza at the apex
      int i0 = sSecStart[k], i1 = sSecStart[k + 1];
      const Sec& c = sSec[k];
      const bool inside = c.turn && ((c.turn > 0) == (side == 1));
      if (inside && abs(c.turn) >= 60) {
        int m = (i0 + i1) / 2, w = (i1 - i0) / 4;
        buildingRun(i0, m - w, side, t, n, 5, 12);
        buildingRun(m + w, i1, side, t, n, 5, 12);
      } else {
        buildingRun(i0, i1, side, t, n, 5, 13);
      }
    }
  }

  // Street lamps every 10 segments, alternating sides
  int lampSide = 0;
  for (int i = 5; i < N - 2; i += 10)
    if (addLamp(i, lampSide)) lampSide ^= 1;

  // Traffic lights at the crossings; billboards down the side streets;
  // trees at the corners of the blocks
  for (int i = 1; i < N; i++) {
    if (!(segments[i].flags & SEGF_SPARE) || (segments[i - 1].flags & SEGF_SPARE)) continue;
    placeSprite(i + 2, 0, SPR_TRAFFIC_LIGHT, 1.30f);
    placeSprite(i + 2, 1, SPR_TRAFFIC_LIGHT, 1.30f);
    placeSprite(i - 1, 0, SPR_STREET_TREE, 1.32f);
    placeSprite(i + 8, 1, SPR_STREET_TREE, 1.32f);
    if (sRng.chance(0.5f)) placeSprite(i + 5, sRng.range(0, 2), SPR_BILLBOARD_A + sRng.range(0, 3), 3.4f);
  }

  // Street furniture per district
  for (int k = 0; k < sSecN; k++) {
    const int z = sSec[k].zone;
    for (int i = sSecStart[k]; i < sSecStart[k + 1]; i++) {
      for (int side = 0; side < 2; side++) {
        const Segment& s = segments[i];
        if (s.isTunnel()) continue;
        const bool built = s.build[side] != 0;
        const uint32_t h = hash32((uint32_t)i * 2654435761u + (uint32_t)side * 97u);
        // sidewalk props stand between the kerb and the facade (|x| 1.2 .. 1.48)
        switch (z) {
          case CZ_BOULEVARD:
            if (i % 16 == side * 8 && built) placeSprite(i, side, SPR_STREET_TREE, 1.32f);
            if (i < 12 && i % 4 == 2) placeSprite(i, side, SPR_FLAG, 1.36f);
            if (i > N - 40 && i % 6 == 3) placeSprite(i, side, SPR_FLAG, 1.36f);
            if (built && h % 29 == 3) placeSprite(i, side, SPR_PLANTER, 1.30f);
            break;
          case CZ_FINANCE:
            if (built && h % 11 == 0) placeSprite(i, side, SPR_PLANTER, 1.30f);
            if (built && h % 97 == 5) placeSprite(i, side, SPR_BUS_STOP, 1.31f);
            if (built && h % 29 == 7) placeSprite(i, side, (h >> 8) & 1 ? SPR_NEON_CYAN : SPR_NEON_PINK, 1.43f);
            break;
          case CZ_OLDTOWN:
            if (built && i % 14 == side * 7) placeSprite(i, side, SPR_STREET_TREE, 1.32f);
            if (built && h % 13 == 3) placeSprite(i, side, (h >> 8) & 1 ? SPR_NEON_PINK : SPR_NEON_CYAN, 1.43f);
            if (built && h % 101 == 9) placeSprite(i, side, SPR_BUS_STOP, 1.31f);
            break;
          case CZ_SHOPPING:
            if (built && h % 7 == 1) placeSprite(i, side, (h >> 8) & 1 ? SPR_NEON_PINK : SPR_NEON_CYAN, 1.43f);
            if (built && h % 17 == 4) placeSprite(i, side, SPR_PLANTER, 1.30f);
            if (built && h % 89 == 11) placeSprite(i, side, SPR_BUS_STOP, 1.31f);
            break;
          case CZ_PARK:
            if (side == 1) {
              if (h % 4 == 0) placeSprite(i, 1, SPR_TREE, sRng.frange(1.9f, 4.5f));
              else if (h % 7 == 1) placeSprite(i, 1, SPR_FLOWERS, sRng.frange(1.4f, 2.6f));
              else if (h % 9 == 2) placeSprite(i, 1, SPR_BUSH, sRng.frange(1.4f, 2.2f));
              else if (h % 29 == 3) placeSprite(i, 1, SPR_BILLBOARD_A + (int)((h >> 9) % 3), 3.2f);
            } else if (built && i % 12 == 3) {
              placeSprite(i, 0, SPR_STREET_TREE, 1.32f);
            }
            break;
          case CZ_HARBOUR:
            if (side == 0 && h % 23 == 0) placeSprite(i, 0, SPR_SAILBOAT, sRng.frange(3.0f, 9.0f));
            if (side == 1 && built && h % 15 == 2) placeSprite(i, 1, SPR_PLANTER, 1.30f);
            break;
          default: break;
        }
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  3b. SUNSET COAST
// ═══════════════════════════════════════════════════════════════
static void decorateCoast() {
  const int N = segCount;
  for (int k = 0; k < sSecN; k++) {
    const int z = sSec[k].zone;
    for (int i = sSecStart[k]; i < sSecStart[k + 1]; i++) {
      int l = MAT_SAND, r = MAT_GRASS;
      switch (z) {
        case KZ_TOWN:       l = MAT_SAND;  r = MAT_SIDEWALK; break;
        case KZ_SEAWALL:    l = MAT_WATER; r = MAT_ROCK; break;
        case KZ_CAPE:       l = MAT_ROCK;  r = MAT_ROCK; break;
        case KZ_LIGHTHOUSE: l = MAT_ROCK;  r = MAT_ROCK; break;
        case KZ_HILLS:      l = MAT_GRASS; r = MAT_GRASS; break;
        default: break;
      }
      segments[i].ground = groundPack(l, r);
      if (l == MAT_WATER) segments[i].flags |= SEGF_RAIL_L;
    }
  }
  markTunnels();

  for (int k = 0; k < sSecN; k++) {
    const int z = sSec[k].zone;
    const int i0 = sSecStart[k], i1 = sSecStart[k + 1];
    switch (z) {
      case KZ_TOWN: {        // villas along the promenade, lamps, palms between
        static const BldPick T[] = { { BLD_VILLA, 60, 95, 1 } };
        for (int i = i0; i < i1;) {
          int len = sRng.range(5, 9), gap = sRng.range(2, 5);
          int h = sRng.range(T[0].hMin, T[0].hMax + 1), v = sRng.range(0, 16);
          for (int j = 0; j < len && i < i1; j++, i++) setBuilding(i, 1, BLD_VILLA, h, v);
          for (int j = 0; j < gap && i < i1; j++, i++)
            if (j == gap / 2) placeSprite(i, 1, SPR_PALM, 1.36f);
        }
        for (int i = i0; i < i1; i++) {
          if (i % 8 == 4) addLamp(i, 1);
          if (i % 4 == 0) {
            int r = sRng.range(0, 10);
            if (r < 5) placeSprite(i, 0, (r & 1) ? SPR_UMBRELLA_B : SPR_UMBRELLA, sRng.frange(1.5f, 2.3f));
            else if (r < 7) placeSprite(i, 0, SPR_PALM, sRng.frange(1.4f, 2.0f));
          }
          if (i % 31 == 7) placeSprite(i, 0, SPR_SAILBOAT, sRng.frange(4.0f, 11.0f));
        }
        break;
      }
      case KZ_BEACH:
        for (int i = i0; i < i1; i++) {
          if (i % 4 == 1) {
            int r = sRng.range(0, 10);
            if (r < 4) placeSprite(i, 0, (r & 1) ? SPR_UMBRELLA_B : SPR_UMBRELLA, sRng.frange(1.6f, 2.35f));
            else if (r < 6) placeSprite(i, 0, SPR_PALM, sRng.frange(1.5f, 2.2f));
          }
          if (i % 37 == 11) placeSprite(i, 0, SPR_SAILBOAT, sRng.frange(3.5f, 12.0f));
          if (i % 5 == 2) {
            int r = sRng.range(0, 12);
            if (r < 5) placeSprite(i, 1, SPR_PALM, sRng.frange(1.6f, 3.4f));
            else if (r < 7) placeSprite(i, 1, SPR_BUSH, sRng.frange(1.4f, 2.6f));
            else if (r < 9) placeSprite(i, 1, SPR_FLOWERS, sRng.frange(1.3f, 2.2f));
            else if (r < 10) placeSprite(i, 1, SPR_ROCK, sRng.frange(1.8f, 3.0f));
          } else if (i % 12 == 0) {
            placeSprite(i, 1, SPR_POST, 1.19f);
          }
        }
        break;
      case KZ_SEAWALL: {      // the road cut into the cliff above the sea, opening into coves
        int sinceCove = 0;
        for (int i = i0; i < i1;) {
          if (sinceCove > 44 && i1 - i > 30 && sRng.chance(0.5f)) {
            int cove = sRng.range(12, 20);           // a cove: grass, rocks and palms
            for (int j = 0; j < cove && i < i1; j++, i++) {
              setMat(i, 1, MAT_GRASS);
              if (j % 3 == 1) placeSprite(i, 1, sRng.chance(0.5f) ? SPR_PALM : SPR_ROCK, sRng.frange(1.5f, 2.8f));
              else if (j % 3 == 2) placeSprite(i, 1, SPR_BUSH, sRng.frange(1.4f, 2.2f));
            }
            sinceCove = 0;
            continue;
          }
          int len = sRng.range(6, 14), h = sRng.range(130, 230), v = sRng.range(0, 16);
          for (int j = 0; j < len && i < i1; j++, i++) setBuilding(i, 1, BLD_CLIFF, h, v);
          sinceCove += len;
        }
        for (int i = i0; i < i1; i++)
          if (i % 41 == 13) placeSprite(i, 0, SPR_SAILBOAT, sRng.frange(3.0f, 10.0f));
        break;
      }
      case KZ_CAPE:
        for (int i = i0; i < i1;) {
          int len = sRng.range(5, 12), h = sRng.range(150, 255), v = sRng.range(0, 16);
          for (int j = 0; j < len && i < i1; j++, i++) setBuilding(i, 1, BLD_CLIFF, h, v);
        }
        for (int i = i0; i < i1; i++) {
          if (i % 6 == 2) placeSprite(i, 0, sRng.chance(0.4f) ? SPR_BOULDER : SPR_ROCK, sRng.frange(1.7f, 3.5f));
          else if (i % 6 == 5) placeSprite(i, 0, SPR_GRASS, sRng.frange(1.3f, 2.4f));
        }
        break;
      case KZ_LIGHTHOUSE: {
        for (int i = i0; i < i1;) {
          int len = sRng.range(5, 12), h = sRng.range(120, 220), v = sRng.range(0, 16);
          for (int j = 0; j < len && i < i1; j++, i++) setBuilding(i, 1, BLD_CLIFF, h, v);
        }
        forceSprite(i0 - 3, 0, SPR_LIGHTHOUSE, 2.5f);      // on the point, seen all along the approach
        for (int i = i0; i < i1; i++) {
          if (i % 5 == 1) placeSprite(i, 0, sRng.chance(0.3f) ? SPR_BOULDER : SPR_ROCK, sRng.frange(1.6f, 2.8f));
          else if (i % 5 == 3) placeSprite(i, 0, SPR_GRASS, sRng.frange(1.3f, 2.2f));
        }
        break;
      }
      case KZ_VILLAGE:
        for (int i = i0; i < i1;) {
          int len = sRng.range(5, 8), gap = sRng.range(2, 6);
          int h = sRng.range(55, 100), v = sRng.range(0, 16);
          for (int j = 0; j < len && i < i1; j++, i++) setBuilding(i, 1, BLD_VILLA, h, v);
          for (int j = 0; j < gap && i < i1; j++, i++) {
            if (j == 0) placeSprite(i, 1, SPR_CYPRESS, 1.36f);
            else if (j == gap - 1) placeSprite(i, 1, sRng.chance(0.5f) ? SPR_PALM : SPR_CYPRESS, 1.40f);
          }
        }
        for (int i = i0; i < i1; i++) {
          if (i % 4 == 2 && sRng.chance(0.6f)) placeSprite(i, 0, (i & 4) ? SPR_UMBRELLA_B : SPR_UMBRELLA, sRng.frange(1.6f, 2.3f));
          if (i % 29 == 5) placeSprite(i, 0, SPR_SAILBOAT, sRng.frange(3.5f, 9.0f));
          if (i % 10 == 6) addLamp(i, 1);
        }
        break;
      case KZ_HILLS:
        for (int i = i0; i < i1; i++) {
          for (int side = 0; side < 2; side++) {
            uint32_t h = hash32((uint32_t)i * 7919u + (uint32_t)side);
            const bool fenceRun = ((i / 36 + side) % 3) == 0;   // runs of fence along the fields
            if (fenceRun && (i & 1) == 0) {
              placeSprite(i, side, SPR_FENCE, 2.7f);
            } else if (h % 4 == 0) {
              int r = (int)((h >> 4) % 10);
              int t = r < 3 ? SPR_CYPRESS : (r < 5 ? SPR_FLOWERS : (r < 7 ? SPR_GRASS : (r < 8 ? SPR_ROCK : SPR_BUSH)));
              placeSprite(i, side, t, sRng.frange(1.4f, fenceRun ? 2.5f : 3.8f));
            } else if (i % 12 == 0) {
              placeSprite(i, side, SPR_POST, 1.19f);
            }
          }
          if (i % 90 == 40) placeSprite(i, 1, SPR_BILLBOARD_A + sRng.range(0, 2), 3.0f);   // no neon by the sea
        }
        break;
      default: break;
    }
  }
  dressTunnelMouths(BLD_CLIFF, 170, 255, 14);
}

// ═══════════════════════════════════════════════════════════════
//  3c. ALPINE PASS
// ═══════════════════════════════════════════════════════════════
static void decorateMountain() {
  const int N = segCount;
  float minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i < N; i++) { minY = fminF(minY, segments[i].y); maxY = fmaxF(maxY, segments[i].y); }
  const float snowLine = minY + 0.58f * (maxY - minY);
  const float treeLine = minY + 0.90f * (maxY - minY);

  for (int k = 0; k < sSecN; k++) {
    const int z = sSec[k].zone;
    for (int i = sSecStart[k]; i < sSecStart[k + 1]; i++) {
      Segment& s = segments[i];
      const int soft = s.y > snowLine ? MAT_SNOW : MAT_GRASS;
      int l = soft, r = soft;
      if (z == MZ_WALL_L) { l = MAT_ROCK; s.flags |= SEGF_RAIL_R; }
      if (z == MZ_WALL_R) { r = MAT_ROCK; s.flags |= SEGF_RAIL_L; }
      if (z == MZ_LAKE)   { l = MAT_WATER; s.flags |= SEGF_RAIL_L; }
      if (z == MZ_HIGH)   { l = r = (s.y > snowLine - 400.0f) ? MAT_SNOW : MAT_GRASS; }
      if (z == MZ_VALLEY && soft == MAT_GRASS && (i % 50) < 3) r = MAT_DIRT;   // farm tracks
      s.ground = groundPack(l, r);
    }
  }
  markTunnels();

  for (int k = 0; k < sSecN; k++) {
    const int z = sSec[k].zone;
    const int i0 = sSecStart[k], i1 = sSecStart[k + 1];
    if (z == MZ_WALL_L || z == MZ_WALL_R) {        // the rock face
      const int side = z == MZ_WALL_L ? 0 : 1;
      for (int i = i0; i < i1;) {
        int len = sRng.range(4, 10), h = sRng.range(170, 255), v = sRng.range(0, 16);
        for (int j = 0; j < len && i < i1; j++, i++) setBuilding(i, side, BLD_CLIFF, h, v);
      }
    }
    for (int i = i0; i < i1; i++) {
      const Segment& s = segments[i];
      const bool snow = s.y > snowLine, bare = s.y > treeLine;
      const int pine = snow ? SPR_PINE_SNOW : SPR_PINE;
      for (int side = 0; side < 2; side++) {
        const uint32_t h = hash32((uint32_t)i * 40503u + (uint32_t)side * 977u);
        const bool rail = (s.flags & railFlag(side)) != 0;
        const float near = rail ? 1.45f : 1.30f;
        switch (z) {
          case MZ_VALLEY: {
            const bool fenceRun = ((i / 40 + side) % 2) == 0;   // pasture fences, alternating sides
            if (fenceRun && (i & 1) == 0) {
              placeSprite(i, side, SPR_FENCE, 2.7f);
            } else if (h % 61 == 13) {
              placeSprite(i, side, SPR_CHALET, sRng.frange(3.6f, 5.0f));
            } else if (h % 5 == 0) {
              int r = (int)((h >> 5) % 10);
              int t = r < 3 ? SPR_FLOWERS : (r < 5 ? SPR_GRASS : (r < 8 ? pine : SPR_BUSH));
              placeSprite(i, side, t, sRng.frange(near, fenceRun ? 2.5f : 4.5f));
            } else if (i % 12 == 6) {
              placeSprite(i, side, snow ? SPR_SNOW_POLE : SPR_POST, 1.19f);
            }
            break;
          }
          case MZ_FOREST:
            if ((h & 1) == 0) placeSprite(i, side, h % 11 == 1 ? SPR_ROCK : pine, sRng.frange(near + 0.15f, 4.2f));
            else if (i % 12 == 6) placeSprite(i, side, snow ? SPR_SNOW_POLE : SPR_POST, 1.19f);
            break;
          case MZ_WALL_L: case MZ_WALL_R:
            if (s.build[side]) {                      // fallen rocks at the foot of the face
              if (h % 17 == 3) placeSprite(i, side, SPR_BOULDER, 1.30f);
            } else if (rail) {                        // the drop: keep the view open over the rail
              if (i % 10 == 5) placeSprite(i, side, SPR_SNOW_POLE, 1.40f);
              else if (h % 23 == 2) placeSprite(i, side, bare ? SPR_ROCK : pine, sRng.frange(1.6f, 2.2f));
            } else if (h % 3 == 0) {
              placeSprite(i, side, bare ? SPR_ROCK : pine, sRng.frange(1.55f, 3.6f));
            }
            break;
          case MZ_HIGH:
            if (i % 8 == (side ? 4 : 0)) placeSprite(i, side, SPR_SNOW_POLE, 1.19f);
            else if (h % 4 == 1) placeSprite(i, side, bare ? (h % 3 ? SPR_ROCK : SPR_BOULDER) : pine,
                                             sRng.frange(1.5f, 4.0f));
            break;
          case MZ_LAKE:
            if (side == 1 && h % 3 == 0) placeSprite(i, 1, h % 5 == 0 ? SPR_BOULDER : pine, sRng.frange(1.4f, 3.5f));
            if (side == 0 && i % 10 == 0) placeSprite(i, 0, SPR_SNOW_POLE, 1.40f);
            break;
          default: break;
        }
      }
    }
  }
  dressTunnelMouths(BLD_CLIFF, 190, 255, 14);
}

// ═══════════════════════════════════════════════════════════════
//  4. MINIMAP
//  Heading integrated from the curve data. The designed circuits close
//  by construction; the normalisation of the total turn to +-2 pi and the
//  linear removal of any residual closure error remain as a safety net.
//  The outline is fitted into 0..255 keeping its aspect ratio.
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

  float (*pts)[2] = sB->pts;
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
  const Design& D = DESIGNS[id];
  const CircuitInfo& info = CIRCUITS[id];
  sRng.setSeed(D.seed);
  sLastH[0] = sLastH[1] = 0;
  if (!scratchInit()) { track_clear(); track_finish(); return; }
  genLayout(D);
  if (segCount <= 0) { plat_log("circuit: no segment buffer\n"); return; }

  switch (info.theme) {
    case THEME_COAST:
      decorateCoast();
      cornerSigns(6.0f, 8.0f);
      break;
    case THEME_MOUNTAIN:
      decorateMountain();
      cornerSigns(5.5f, 8.5f);
      break;
    default:
      decorateCity();
      dressTunnelMouths(BLD_WALL, 34, 44, 6);
      cornerSigns(6.5f, 8.0f);
      break;
  }

  // Start/finish line; nothing in the way on the grid; scratch flags off
  segments[0].flags |= SEGF_START;
  for (int i = 0; i < segCount; i++) {
    Segment& s = segments[i];
    s.flags &= (uint8_t)~SEGF_SPARE;
    if (i > segCount - 60 || i < 6) s.flags &= (uint8_t)~SEGF_CROSSWALK;
    if (s.isTunnel())
      s.flags &= (uint8_t)~(SEGF_LAMP_L | SEGF_LAMP_R | SEGF_RAIL_L | SEGF_RAIL_R | SEGF_CROSSWALK);
    s.deco = 0;
    s.pad = 0;
  }

  buildMinimap();
  setTheme(info.theme);
  initBackground(info.theme);
  setDayCycle(info.cycleStart);

  // Summary for the log: layout and decoration counts
  int bl = 0, br = 0, tun = 0, rails = 0, cw = 0, lamps = 0, spr = 0;
  float minY = 0, maxY = 0, maxC = 0, maxSlope = 0;
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
    maxC = fmaxF(maxC, fabsf(s.curve));
    maxSlope = fmaxF(maxSlope, fabsf(s.y - segAt(i - 1).y) / SEG_LEN);
  }
  float ex, ey;
  integratePlan(ex, ey, nullptr);
  plat_log("circuit %d %s: %d segments, closure %.2f/%.2f, max curve %.2f, slope %.3f, "
           "buildings L%d%% R%d%%, tunnel %d, rails %d, crosswalks %d, lamps %d, sprites %d, "
           "elevation %.0f..%.0f\n",
           id, info.name, segCount, ex, ey, maxC, maxSlope, bl * 100 / segCount, br * 100 / segCount, tun,
           rails, cw, lamps, spr, minY, maxY);
}
