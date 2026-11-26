/*
  ═══════════════════════════════════════════════════════════════
  RIVAL CARS IN THE ROAD PASS — implementation

  Each visible rival is assigned to the slice that contains its REAR
  bumper (the part nearest to the camera): nearer road rows are drawn
  later and lie below that point, so they never paint over the car,
  while everything farther (road, scenery) is already drawn under it.
  The car itself is anchored at its centre (renderCar's ground centre).
  ═══════════════════════════════════════════════════════════════
*/

#include "render_cars.h"
#include "render_road.h"
#include "render_traffic.h"
#include "render_player.h"
#include "race.h"
#include "track.h"
#include "colors.h"
#include "utils.h"
#include <math.h>

struct CarSlot { int8_t racer; int16_t n; float rel; };
static CarSlot sSlots[NUM_RACERS];   // drawn by the road pass, far -> near
static CarSlot sNear[NUM_RACERS];    // nearer than the player's car, drawn after it
static int sCount = 0, sCursor = 0, sNearCount = 0;

static void sortFarToNear(CarSlot* a, int n) {
  for (int i = 1; i < n; i++) {
    CarSlot k = a[i];
    int j = i - 1;
    while (j >= 0 && a[j].rel < k.rel) { a[j + 1] = a[j]; j--; }
    a[j + 1] = k;
  }
}

void cars_prepare(float position) {
  sCount = 0;
  sCursor = 0;
  sNearCount = 0;
  if (segCount <= 0) return;
  const float base = position - roadPosOff;            // track z of boundary 0
  const float maxRel = (DRAW_DIST - 1) * (float)SEG_LEN;
  float relPlayer = wrapDelta(base, racers[PLAYER_ID].z, trackLength);
  if (relPlayer < 0) relPlayer += trackLength;
  for (int i = 0; i < NUM_RACERS; i++) {
    if (i == PLAYER_ID) continue;
    float rel = wrapDelta(base, racers[i].z, trackLength);
    if (rel < 0) rel += trackLength;                   // [0, trackLength)
    if (rel >= maxRel) continue;
    if (rel - roadPosOff <= ROAD_NEAR_Z * 4.0f) continue;   // centre too close / behind
    int n = (int)floorf((rel - CAR_LEN * 0.5f) / SEG_LEN);
    if (n < 1) n = 1;
    if (n > DRAW_DIST - 1) n = DRAW_DIST - 1;
    CarSlot& c = (rel < relPlayer) ? sNear[sNearCount++] : sSlots[sCount++];
    c.racer = (int8_t)i;
    c.n = (int16_t)n;
    c.rel = rel;
  }
  sortFarToNear(sSlots, sCount);
  sortFarToNear(sNear, sNearCount);
}

static void drawRacer(const CarSlot& cs) {
  const Racer& R = racers[cs.racer];
  float fz = cs.rel / SEG_LEN;
  int k = (int)fz;
  if (k < 0 || k >= DRAW_DIST) return;
  float frac = fz - (float)k;
  float wz = roadBZ[k] + frac * SEG_LEN;
  if (wz <= ROAD_NEAR_Z * 4.0f) return;
  float wy = lerpF(roadBY[k], roadBY[k + 1], frac);
  float wx = lerpF(roadBX[k], roadBX[k + 1], frac) + R.x * ROAD_W;
  ProjPt p;
  roadProject(p, wx, wy, wz);
  bool tunnel = segAt(roadBaseIdx + k).isTunnel();
  float fog = expFog(wz / ((float)DRAW_DIST * SEG_LEN), FOG_DENSITY);
  float heading = atanf((roadBX[k + 1] - roadBX[k]) / SEG_LEN) + R.yaw;
  // 0.7: the road projection stretches x by 4:3, the full angle overdoes it
  float view = atan2f(wx - roadCam.camX, wz) * 0.7f;
  // The player's car is drawn from a fixed, flatter viewpoint (pitch 0.30,
  // eye 11 model units away) than the real camera geometry. Near rivals use
  // the same viewpoint so they look exactly like the player's car; far ones
  // converge to their true (smaller) viewing angle on their own.
  float pitch = fminF(atan2f(roadCam.camY - wy, wz), PLAYER_VIEW_PITCH) +
                atanf((roadBY[k + 1] - roadBY[k]) / SEG_LEN);
  float camDist = fmaxF(PLAYER_VIEW_DIST, wz / 234.0f);
  drawTrafficCar(p.x, p.y, p.scale, R.paint, fog, heading - view, pitch, tunnel, camDist, R.braking);
}

void cars_drawSlice(int n, float fog, bool tunnel) {
  (void)fog;
  (void)tunnel;
  while (sCursor < sCount && sSlots[sCursor].n > n) sCursor++;   // slice was not drawn
  while (sCursor < sCount && sSlots[sCursor].n == n) drawRacer(sSlots[sCursor++]);
}

void cars_drawNear() {
  for (int i = 0; i < sNearCount; i++) drawRacer(sNear[i]);
}
