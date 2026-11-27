/*
  ═══════════════════════════════════════════════════════════════
  RIVAL CARS IN THE ROAD PASS — implementation

  Two cameras.
  * The ROAD camera (render_road) is high: it sees the player's car from
    ~50 degrees above. The player's car itself is drawn from a flatter
    "CAR camera" (pitch 0.30, eye 11 model units behind, anchor at
    PLAYER_ANCHOR_Y), which is what makes it look good.
  * A rival drawn with the road camera next to the player's car was 13 px
    lower, and touching nose to tail it showed a ~20 px gap; half a car
    length behind it was below the screen. So rivals near the player are
    drawn with the CAR camera (exactly the player's view: same size, same
    baseline, bumpers touch when the footprints touch), and from one to
    four car lengths ahead the view blends smoothly into the road camera,
    which is the truth for the road.

  Order. Each rival is drawn inside the slice of the road pass that holds
  the lowest row it covers (its rear shadow), so nearer road rows never
  paint over it and everything farther is already drawn; cars sorted far
  to near never swap. Rivals whose centre is behind the player's car
  centre are drawn after the player (cars_drawNear).
  Cost. A car is culled before any vertex is transformed: off screen,
  behind the near plane, or entirely below the slice's clip row (behind a
  hill crest). LOD: full mesh / cheaper lighting (< 40 px wide) / sprite
  (< 10 px wide), with hysteresis so a car never flickers between two.
  ═══════════════════════════════════════════════════════════════
*/

#include "render_cars.h"
#include "render_road.h"
#include "render_traffic.h"
#include "render_player.h"
#include "race.h"
#include "physics.h"
#include "track.h"
#include "colors.h"
#include "gfx.h"
#include "utils.h"
#include <math.h>

// ═══════════════════════════════════════════════════════════════
//  TUNING
// ═══════════════════════════════════════════════════════════════
static const float MU       = TRAFFIC_PX_PER_SCALE / SCR_CX;  // world units per model unit (168.75)
static const float CC_NEAR  = CAR_LEN * 1.1f;   // pure car camera up to this far ahead
static const float CC_FAR   = CAR_LEN * 4.5f;   // pure road camera from here on
static const float LOD_HYST = 0.12f;            // +-12 % around the LOD widths

struct CarSlot {
  int8_t  racer;
  int16_t n;           // road slice it is drawn in
  float   dz;          // centre ahead of the player's car centre (world units)
  float   yTop;        // highest screen row (to cull behind crests)
  CarView v;
};
static CarSlot sSlots[NUM_RACERS];   // drawn by the road pass, far -> near
static CarSlot sNear[NUM_RACERS];    // behind the player's car centre, drawn after it
static int sCount = 0, sCursor = 0, sNearCount = 0;
static uint8_t sLod[NUM_RACERS];     // current LOD per racer (hysteresis)
static float   sFlame[NUM_RACERS];   // smoothed turbo flame

static inline float smooth01(float t) {
  t = clampF(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Road geometry at a distance `rel` from boundary 0 (camera frame):
// centre x, elevation, camera z and heading of the centre line.
struct RoadPt { float x, y, z, heading; };
static bool roadAt(float rel, RoadPt& p) {
  float fz = rel / SEG_LEN;
  int k = (int)floorf(fz);
  if (k < 0 || k >= DRAW_DIST) return false;
  float f = fz - (float)k;
  p.x = lerpF(roadBX[k], roadBX[k + 1], f);
  p.y = lerpF(roadBY[k], roadBY[k + 1], f);
  p.z = roadBZ[k] + f * SEG_LEN;
  // centre-line slope at the two boundaries (central differences), blended
  int a0 = imax(k - 1, 0), a1 = imin(k + 1, DRAW_DIST), b0 = k, b1 = imin(k + 2, DRAW_DIST);
  float s0 = (roadBX[a1] - roadBX[a0]) / (SEG_LEN * (float)(a1 - a0));
  float s1 = (roadBX[b1] - roadBX[b0]) / (SEG_LEN * (float)imax(b1 - b0, 1));
  p.heading = atanf(lerpF(s0, s1, f));
  return true;
}

// Screen row of boundary n (+inf when it is behind the near plane)
static float boundaryY(int n) {
  if (n < 0 || n > DRAW_DIST || roadBZ[n] <= ROAD_NEAR_Z) return 1e9f;
  ProjPt p;
  roadProject(p, roadBX[n], roadBY[n], roadBZ[n]);
  return p.y;
}

static void sortFarToNear(CarSlot* a, int n) {
  for (int i = 1; i < n; i++) {
    CarSlot k = a[i];
    int j = i - 1;
    while (j >= 0 && a[j].dz < k.dz) { a[j + 1] = a[j]; j--; }
    a[j + 1] = k;
  }
}

// Fill the view of racer i; false when it cannot be seen at all.
static bool computeView(int i, float base, const RoadPt& pp, CarSlot& c) {
  const Racer& R = racers[i];
  const Racer& P = racers[PLAYER_ID];
  float rel = wrapDelta(base, R.z, trackLength);
  if (rel < 0) rel += trackLength;
  if (rel >= (DRAW_DIST - 1) * (float)SEG_LEN) return false;      // beyond the draw distance
  const float dz = wrapDelta(P.z, R.z, trackLength);
  if (dz < -CAR_LEN * 1.3f) return false;                          // behind the player: below the screen
  RoadPt rp;
  if (!roadAt(rel, rp)) return false;
  const float wx = rp.x + R.x * ROAD_W;

  // axle slope (both wheels on the road), and the player's
  RoadPt ra, rb, pa, pb;
  float slopeR = 0, slopeP = 0;
  if (roadAt(fmaxF(rel - CAR_LEN * 0.5f, 0.0f), ra) && roadAt(rel + CAR_LEN * 0.5f, rb))
    slopeR = atanf((rb.y - ra.y) / fmaxF(rb.z - ra.z, 1.0f));
  float relP = wrapDelta(base, P.z, trackLength);
  if (roadAt(fmaxF(relP - CAR_LEN * 0.5f, 0.0f), pa) && roadAt(relP + CAR_LEN * 0.5f, pb))
    slopeP = atanf((pb.y - pa.y) / fmaxF(pb.z - pa.z, 1.0f));

  // ---- road camera (true projection) ----
  float rcX = 0, rcY = 0, rcPpu = 0, rcDist = 0, rcPitch = 0, rcYaw = 0;
  const bool rcOk = rp.z > ROAD_NEAR_Z * 4.0f;
  if (rcOk) {
    ProjPt p;
    roadProject(p, wx, rp.y, rp.z);
    const float eyeDy = roadCam.camY - rp.y;
    rcX = p.x;
    rcY = p.y;
    rcPpu = p.scale * TRAFFIC_PX_PER_SCALE;
    rcDist = sqrtf(rp.z * rp.z + eyeDy * eyeDy) / MU;
    rcPitch = fminF(atan2f(eyeDy, rp.z), PLAYER_VIEW_PITCH) + slopeR;
    // 0.7: the road projection stretches x by 4:3, the full angle overdoes it
    rcYaw = rp.heading + R.yaw - atan2f(wx - roadCam.camX, rp.z) * 0.7f;
  }

  // ---- car camera (the player's car view) ----
  const float th = PLAYER_VIEW_PITCH, cth = cosf(th), sth = sinf(th);
  const float E = PLAYER_VIEW_DIST * cth * MU, H = PLAYER_VIEW_DIST * sth * MU;
  const float F = PLAYER_PX_PER_UNIT * PLAYER_VIEW_DIST;
  const float dX = wx - (pp.x + P.x * ROAD_W), dY = rp.y - pp.y;
  const float h = E + dz, vv = dY - H;
  const float zc = h * cth - vv * sth;
  const float yc = vv * cth + h * sth;
  const bool ccOk = zc > 3.0f * MU;
  float ccX = 0, ccY = 0, ccPpu = 0, ccDist = 0, ccPitch = 0, ccYaw = 0;
  if (ccOk) {
    ccX = SCR_CX + steerVisual * PLAYER_STEER_PX + F * dX / zc;
    ccY = PLAYER_ANCHOR_Y - F * yc / zc;
    ccPpu = F * MU / zc;
    ccDist = zc / MU;
    ccPitch = atan2f(H - dY, h) + playerViewSlope() + 0.6f * (slopeR - slopeP);
    ccYaw = (rp.heading - pp.heading) + R.yaw - atan2f(dX, h);
  }

  // ---- blend: car camera near the player, road camera far away ----
  float w = 1.0f - smooth01((dz - CC_NEAR) / (CC_FAR - CC_NEAR));
  if (!ccOk) w = 0.0f;
  if (!rcOk) w = 1.0f;
  if (!ccOk && !rcOk) return false;
  CarView& v = c.v;
  v.sx = lerpF(rcX, ccX, w);
  v.sy = lerpF(rcY, ccY, w);
  v.ppu = lerpF(rcPpu, ccPpu, w);
  v.camDist = lerpF(rcDist, ccDist, w);
  v.pitch = lerpF(rcPitch, ccPitch, w);
  v.yaw = lerpF(rcYaw, ccYaw, w);
  v.roll = 0;
  // front wheels into the curve and the lane change; the body leans out of it
  const float steer = clampF(R.yaw * 1.5f + segments[findSegIdx(R.z)].curve * 0.12f, -1.0f, 1.0f);
  v.wheelSteer = steer * CAR_WHEEL_STEER;
  v.bodyRoll = -steer * CAR_BODY_ROLL * clampF(R.speed / maxSpeed, 0.0f, 1.0f);
  if (v.ppu <= 0.05f || v.ppu > 90.0f) return false;                // guard: absurd sizes

  // ---- screen extent; cull before any vertex is transformed ----
  const float sp = sinf(v.pitch), cp = cosf(v.pitch);
  const float back = fmaxF(v.camDist - 3.72f * cp, 0.6f);
  const float yLow = v.sy + 3.72f * sp * v.ppu * v.camDist / back;  // rear of the shadow
  c.yTop = v.sy - (2.3f * cp + 3.3f * sp) * v.ppu * 1.15f;          // roof / far end
  const float halfW = 3.6f * v.ppu;
  if (v.sx + halfW < 0 || v.sx - halfW > SCR_W || yLow < 0 || c.yTop > SCR_H) return false;

  // ---- look ----
  const int k = imin((int)(rel / SEG_LEN), DRAW_DIST - 1);
  v.tunnel = segAt(roadBaseIdx + k).isTunnel();
  v.fog = expFog(rp.z / ((float)DRAW_DIST * SEG_LEN), FOG_DENSITY);
  v.paint = R.paint;
  v.lights = pal.night > 0.3f || v.tunnel;
  v.brake = R.braking;
  sFlame[i] += ((R.boosting ? 1.0f : 0.0f) - sFlame[i]) * 0.5f;
  v.flame = sFlame[i];
  // LOD by width, with hysteresis
  const float wpx = 2.9f * v.ppu;
  uint8_t& L = sLod[i];
  if (L == CAR_VIEW_FULL && wpx < CAR_MIDLOD_W * (1.0f - LOD_HYST)) L = CAR_VIEW_MID;
  if (L == CAR_VIEW_MID && wpx > CAR_MIDLOD_W * (1.0f + LOD_HYST)) L = CAR_VIEW_FULL;
  if (L == CAR_VIEW_MID && wpx < CAR_SPRITE_W * (1.0f - LOD_HYST)) L = CAR_VIEW_SPRITE;
  if (L == CAR_VIEW_SPRITE && wpx > CAR_SPRITE_W * (1.0f + LOD_HYST)) L = CAR_VIEW_MID;
  if (L > CAR_VIEW_SPRITE) L = CAR_VIEW_FULL;
  v.lod = L;

  // ---- slice: the one whose near boundary is at or below the car's lowest row ----
  int n = (int)floorf((rel - CAR_LEN * 0.5f) / SEG_LEN);
  n = iclamp(n, 1, DRAW_DIST - 1);
  while (n > 1 && boundaryY(n) < yLow) n--;
  c.n = (int16_t)n;
  c.dz = dz;
  c.racer = (int8_t)i;
  return true;
}

void cars_prepare(float position) {
  sCount = 0;
  sCursor = 0;
  sNearCount = 0;
  if (segCount <= 0) return;
  const float base = position - roadPosOff;            // track z of boundary 0
  RoadPt pp;
  float relP = wrapDelta(base, racers[PLAYER_ID].z, trackLength);
  if (!roadAt(relP, pp)) { pp.x = 0; pp.y = roadCam.camY - CAM_HEIGHT; pp.z = playerZdist; pp.heading = 0; }
  for (int i = 0; i < NUM_RACERS; i++) {
    if (i == PLAYER_ID) continue;
    CarSlot c;
    if (!computeView(i, base, pp, c)) continue;
    if (c.dz < 0) sNear[sNearCount++] = c;
    else          sSlots[sCount++] = c;
  }
  sortFarToNear(sSlots, sCount);
  sortFarToNear(sNear, sNearCount);
  // a nearer car must never be drawn in a farther slice than a farther car
  for (int k = 1; k < sCount; k++)
    if (sSlots[k].n > sSlots[k - 1].n) sSlots[k].n = sSlots[k - 1].n;
}

static void drawSlot(const CarSlot& c) {
  if (c.yTop >= gfx_cy1) return;     // entirely below the clip row: hidden by a crest
  drawCarView(c.v, raceTime);
}

void cars_drawSlice(int n, float fog, bool tunnel) {
  (void)fog;
  (void)tunnel;
  while (sCursor < sCount && sSlots[sCursor].n > n) {
    // its slice was not drawn (invalid / behind the near plane): draw it now
    drawSlot(sSlots[sCursor++]);
  }
  while (sCursor < sCount && sSlots[sCursor].n == n) drawSlot(sSlots[sCursor++]);
}

void cars_drawNear() {
  // the road pass is over: anything it did not reach is drawn here too
  while (sCursor < sCount) drawSlot(sSlots[sCursor++]);
  for (int i = 0; i < sNearCount; i++) drawCarView(sNear[i].v, raceTime);
}
