/*
  ═══════════════════════════════════════════════════════════════
  RACE STATE — implementation
  Grid, progress, laps (sub-frame crossing times), finish, places every
  frame, and the final classification with estimated times for racers
  that have not finished when the results appear.
  ═══════════════════════════════════════════════════════════════
*/

#include "race.h"
#include "circuits.h"
#include "track.h"
#include "physics.h"
#include "rivals.h"
#include "config.h"
#include "utils.h"
#include <math.h>
#include <string.h>

Racer racers[NUM_RACERS];
float raceTime    = 0;
bool  raceRunning = false;
int   difficulty  = 1;

static float sPrevZ[NUM_RACERS];
static float sStartProgress[NUM_RACERS];
static int   sOrder[NUM_RACERS];

struct RacerLook { const char* name; uint16_t paint; };
static const RacerLook LOOKS[NUM_RACERS] = {
  { "YOU",   PLAYER_PAINT },
  { "VIPER", 0xD0A3 },   // red
  { "NOVA",  0xFE41 },   // yellow
  { "BLITZ", 0x2E68 },   // green
  { "RAVEN", 0xD6DB },   // silver
  { "FURY",  0xFBE0 },   // orange
};

float race_gridProgress(int slot) {
  // staggered grid: consecutive slots half a row apart, alternating columns
  return -(GRID_FRONT + slot * (GRID_ROW * 0.5f));
}

float race_gridX(int slot) {
  return (slot & 1) ? GRID_X : -GRID_X;
}

float race_distance() {
  return (float)curCircuit().laps * trackLength;
}

static float wrapZ(float z) {
  return loopIncrease(z, 0, trackLength);
}

void race_syncPlayer() {
  Racer& P = racers[PLAYER_ID];
  P.z = wrapZ(position + playerZdist);
  P.x = playerX;
  P.speed = speed;
  P.braking = braking;
  P.boosting = boosting;
  P.yaw = steerVisual * 0.20f + driftAngle * 0.35f;
}

void race_init() {
  raceTime = 0;
  raceRunning = false;
  for (int i = 0; i < NUM_RACERS; i++) {
    Racer& R = racers[i];
    memset(&R, 0, sizeof(R));
    strncpy(R.name, LOOKS[i].name, sizeof(R.name) - 1);
    R.paint = LOOKS[i].paint;
    // rivals 1..5 take grid slots 0..4, the player starts last
    int slot = (i == PLAYER_ID) ? NUM_RACERS - 1 : i - 1;
    R.progress = race_gridProgress(slot);
    R.x = race_gridX(slot);
    R.z = wrapZ(R.progress);
    R.place = slot + 1;
    sPrevZ[i] = R.z;
    sStartProgress[i] = R.progress;
    sOrder[slot] = i;
  }
  // The camera sits playerZdist behind the player's car
  physics_placePlayer(wrapZ(racers[PLAYER_ID].z - playerZdist), racers[PLAYER_ID].x);
  rivals_init();
}

void race_go() {
  raceRunning = true;
  raceTime = 0;
}

static bool ahead(const Racer& a, const Racer& b) {
  if (a.finished != b.finished) return a.finished;
  if (a.finished) return a.finishTime < b.finishTime;
  return a.progress > b.progress;
}

void race_update(float dt) {
  if (raceRunning) raceTime += dt;
  const int laps = curCircuit().laps;

  race_syncPlayer();

  for (int i = 0; i < NUM_RACERS; i++) {
    Racer& R = racers[i];
    float dz = wrapDelta(sPrevZ[i], R.z, trackLength);
    if (fabsf(dz) < trackLength * 0.25f) R.progress += dz;   // teleports are not counted
    sPrevZ[i] = R.z;
    if (!raceRunning) continue;

    int completed = (R.progress > 0 && trackLength > 0) ? (int)(R.progress / trackLength) : 0;
    while (!R.finished && R.lap < completed) {
      R.lap++;
      // sub-frame crossing time
      float over = R.progress - R.lap * trackLength;
      float tCross = raceTime - (R.speed > 1.0f ? over / R.speed : 0.0f);
      if (tCross < R.lapStart) tCross = R.lapStart;
      R.lastLap = tCross - R.lapStart;
      if (R.bestLap <= 0 || R.lastLap < R.bestLap) R.bestLap = R.lastLap;
      R.lapStart = tCross;
      if (R.lap >= laps) { R.finished = true; R.finishTime = tCross; }
    }
  }

  // Places: insertion sort of the running order (6 entries)
  for (int i = 1; i < NUM_RACERS; i++) {
    int k = sOrder[i], j = i - 1;
    while (j >= 0 && ahead(racers[k], racers[sOrder[j]])) { sOrder[j + 1] = sOrder[j]; j--; }
    sOrder[j + 1] = k;
  }
  for (int p = 0; p < NUM_RACERS; p++) racers[sOrder[p]].place = p + 1;
}

int race_order(int place) {
  return sOrder[iclamp(place - 1, 0, NUM_RACERS - 1)];
}

float race_lapTime(int i) {
  if (i < 0 || i >= NUM_RACERS || !raceRunning) return 0.0f;
  const Racer& R = racers[i];
  return R.progress > 0 ? fmaxF(0.0f, raceTime - R.lapStart) : 0.0f;
}

void race_results(ResultRow out[NUM_RACERS]) {
  const float dist = race_distance();
  for (int p = 0; p < NUM_RACERS; p++) {
    const int i = sOrder[p];
    const Racer& R = racers[i];
    ResultRow& row = out[p];
    row.racer = (uint8_t)i;
    row.finished = R.finished;
    row.bestLap = R.bestLap;
    if (R.finished) {
      row.time = R.finishTime;
    } else {
      // Pace: the last full lap is the best predictor (racing speed, no
      // standing start); before that, the average since the start.
      float pace = 0.0f;
      if (R.lastLap > 1.0f) pace = trackLength / R.lastLap;
      if (pace <= 0.0f) {
        float covered = R.progress - sStartProgress[i];
        if (raceTime > 1.0f && covered > 0) pace = covered / raceTime;
      }
      if (pace < maxSpeed * 0.3f) pace = maxSpeed * 0.3f;
      float remain = dist - R.progress;
      if (remain < 0) remain = 0;
      row.time = raceTime + remain / pace;
    }
  }
  // Estimates must never beat a real time of a racer classified ahead
  for (int p = 1; p < NUM_RACERS; p++)
    if (out[p].time < out[p - 1].time) out[p].time = out[p - 1].time + 0.01f;
}
