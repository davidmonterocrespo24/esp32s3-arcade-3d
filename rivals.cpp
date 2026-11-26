/*
  ═══════════════════════════════════════════════════════════════
  RIVAL AI — implementation (basic version)

  Speed: exactly the player's throttle model (ramped acceleration,
  damping near the top, friction, gravity on slopes) with a per-rival top
  speed (pace), braking for the corner ahead and for the car ahead. Line: inside of the curvature ahead + a personal bias.
  Traffic: the nearest car ahead in the same lane makes the rival
  either commit to an overtaking line on a free side or follow at a
  gap. Rivals never overlap each other (lateral separation).
  ═══════════════════════════════════════════════════════════════
*/

#include "rivals.h"
#include "race.h"
#include "physics.h"
#include "track.h"
#include "circuits.h"
#include "utils.h"
#include <math.h>

struct Brain {
  float pace;          // top speed = pace * maxSpeed (1.0 = the player's car)
  float bias;          // personal lateral line offset
  float reaction;      // start delay after GO (s)
  float accel;         // throttle state (like the player's `acceleration`)
  float xVel;          // lateral velocity (half-widths / s)
  float passX, passT;  // committed overtaking line and how long to hold it
};
static Brain sBrain[NUM_RACERS];

static inline float perFrame(float f, float dt) { return powf(f, dt * 60.0f); }

// ═══════════════════════════════════════════════════════════════
//  SHARED HELPERS
// ═══════════════════════════════════════════════════════════════
float ai_lineTarget(float z, float bias) {
  if (segCount <= 0) return bias;
  int s = findSegIdx(z);
  // curvature 4..30 segments ahead, nearer corners weigh more
  float acc = 0, wsum = 0;
  for (int k = 4; k <= 30; k += 2) {
    float w = 1.0f - (k - 4) * (0.7f / 26.0f);
    acc += segAt(s + k).curve * w;
    wsum += w;
  }
  float c = wsum > 0 ? acc / wsum : 0;
  return clampF(c * 0.11f, -0.62f, 0.62f) + bias;
}

float ai_cornerSpeed(float z) {
  if (segCount <= 0) return 1.0f;
  int s = findSegIdx(z);
  float cmax = 0;
  for (int k = 0; k <= 24; k += 2) cmax = fmaxF(cmax, fabsf(segAt(s + k).curve));
  // up to |curve| ~6 a car can hold the line flat out; tighter needs lifting
  return clampF(1.0f - (cmax - 6.2f) * 0.075f, 0.62f, 1.0f);
}

// Is lateral position x free of other cars close to racer i?
static bool laneFree(int i, float x) {
  const Racer& R = racers[i];
  for (int j = 0; j < NUM_RACERS; j++) {
    if (j == i) continue;
    float dz = wrapDelta(R.z, racers[j].z, trackLength);
    if (dz < -CAR_LEN * 1.2f || dz > CAR_LEN * 2.2f) continue;
    if (fabsf(racers[j].x - x) < CAR_HALF_W * 2.0f + 0.06f) return false;
  }
  return true;
}

// ═══════════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════════
void rivals_init() {
  Rng rng(0x5EED0000u + (uint32_t)currentCircuit * 131u + (uint32_t)difficulty * 7u);
  // Pace = top speed relative to the player's car (1.0 = identical car).
  static const float BASE[3] = { 0.955f, 0.995f, 1.025f };
  // grid order P1..P5: the pole sitter is the quickest
  static const float SPREAD[NUM_RIVALS] = { 0.025f, 0.015f, 0.005f, -0.005f, -0.015f };
  const int d = iclamp(difficulty, 0, 2);
  for (int i = 0; i < NUM_RACERS; i++) {
    Brain& B = sBrain[i];
    B.pace = BASE[d] + (i > 0 ? SPREAD[i - 1] : 0) + rng.frange(-0.004f, 0.004f);
    B.bias = rng.frange(-0.22f, 0.22f);
    B.reaction = rng.frange(0.10f, 0.45f);
    B.accel = 0;
    B.xVel = 0;
    B.passX = 0;
    B.passT = 0;
  }
}

// ═══════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════
void rivals_update(float dt) {
  if (segCount <= 0) return;
  for (int i = 1; i < NUM_RACERS; i++) {
    Racer& R = racers[i];
    Brain& B = sBrain[i];
    if (!raceRunning || raceTime < B.reaction) {
      R.speed = 0;
      R.braking = false;
      continue;
    }

    // ---- speeds: this car's top speed (pace, a little form variation),
    //      the corner ahead, and (below) the car ahead ----
    float pace = B.pace * (1.0f + 0.006f * sinf(raceTime * 0.21f + i * 1.7f));
    if (R.finished) pace *= 0.70f;                   // cool-down lap
    const float top = maxSpeed * pace;
    float vTarget = top * ai_cornerSpeed(R.z);

    // ---- line ----
    float tx = ai_lineTarget(R.z, B.bias);

    // ---- traffic: nearest car ahead in our lane ----
    int ahead = -1;
    float aheadDz = 1e9f;
    for (int j = 0; j < NUM_RACERS; j++) {
      if (j == i) continue;
      float dz = wrapDelta(R.z, racers[j].z, trackLength);
      if (dz <= 0 || dz > CAR_LEN * 3.0f) continue;
      if (fabsf(racers[j].x - R.x) > CAR_HALF_W * 2.0f + 0.10f) continue;
      if (dz < aheadDz) { aheadDz = dz; ahead = j; }
    }
    if (ahead >= 0) {
      const Racer& A = racers[ahead];
      if (B.passT <= 0 && vTarget > A.speed + 150.0f) {
        float left = A.x - 0.40f, right = A.x + 0.40f;
        bool lOk = left > -0.80f && laneFree(i, left);
        bool rOk = right < 0.80f && laneFree(i, right);
        if (lOk || rOk) {
          B.passX = (lOk && (!rOk || fabsf(left - tx) < fabsf(right - tx))) ? left : right;
          B.passT = 1.8f;
        }
      }
      // follow at a gap while still behind it in the same lane
      float gap = aheadDz - CAR_LEN;
      if (fabsf(A.x - R.x) < CAR_HALF_W * 2.0f + 0.04f && gap < CAR_LEN * 0.9f) {
        float k = clampF(gap / (CAR_LEN * 0.9f), 0, 1);
        vTarget = fminF(vTarget, A.speed - 300.0f * (1.0f - k));
      }
    }
    if (B.passT > 0) { tx = B.passX; B.passT -= dt; }
    // never steer into a car alongside: stay on our side of it
    for (int j = 0; j < NUM_RACERS; j++) {
      if (j == i) continue;
      const Racer& O = racers[j];
      if (fabsf(wrapDelta(R.z, O.z, trackLength)) > CAR_LEN * 1.05f) continue;
      const float sep = CAR_HALF_W * 2.0f + 0.03f;
      if (O.x >= R.x) tx = fminF(tx, O.x - sep);
      else            tx = fmaxF(tx, O.x + sep);
    }
    tx = clampF(tx, -0.80f, 0.80f);

    // ---- longitudinal: exactly the player's car (handleInput +
    //      updatePhysics) with this rival's top speed, so pace 1.0 is a car
    //      identical to the player's: same start, same loss on climbs ----
    int seg = findSegIdx(R.z);
    float slope = (segAt(seg).y - segAt(seg - 1).y) / SEG_LEN;
    float over = R.speed - vTarget;
    R.braking = over > 400.0f;
    if (R.braking) {                                 // corner / car ahead: brake
      B.accel = 0;
      R.speed -= fminF(over, BRAKE_DECEL * dt);
    } else if (R.speed < top * ACCEL_NEAR_MAX && over < 0) {
      B.accel = fminF(B.accel + ACCEL_RAMP * dt, maxSpeed * ACCEL_TARGET);
    } else {
      B.accel *= perFrame(ACCEL_DAMPING, dt);
    }
    R.speed *= perFrame(FRICTION, dt);
    R.speed += B.accel * dt;
    R.speed -= slope * GRAVITY_FACTOR * dt;
    if (over > 0 && !R.braking) R.speed -= fminF(over, 2500.0f * dt);   // lift off
    R.speed = clampF(R.speed, 0, top);

    // ---- lateral: steer towards the target with a limited rate ----
    float want = clampF((tx - R.x) * 2.6f, -1.1f, 1.1f);
    B.xVel += (want - B.xVel) * clampF(dt * 6.0f, 0, 1);
    R.x += B.xVel * dt;
    float yawT = atan2f(B.xVel * ROAD_W, fmaxF(R.speed, 800.0f));
    R.yaw += (yawT - R.yaw) * clampF(dt * 8.0f, 0, 1);

    R.z = loopIncrease(R.z, R.speed * dt, trackLength);
  }

  // ---- separation: rivals never overlap each other ----
  for (int i = 1; i < NUM_RACERS; i++) {
    for (int j = i + 1; j < NUM_RACERS; j++) {
      Racer& A = racers[i];
      Racer& C = racers[j];
      float dz = wrapDelta(A.z, C.z, trackLength);   // + : C ahead of A
      float dx = C.x - A.x;
      if (fabsf(dz) >= CAR_LEN || fabsf(dx) >= CAR_HALF_W * 2.0f) continue;
      float ox = CAR_HALF_W * 2.0f - fabsf(dx);
      float oz = CAR_LEN - fabsf(dz);
      if (oz / CAR_LEN < ox / (CAR_HALF_W * 2.0f)) {
        // nose to tail: the car behind drops back and matches speed
        Racer& rear = dz > 0 ? A : C;
        Racer& front = dz > 0 ? C : A;
        rear.z = loopIncrease(rear.z, -oz, trackLength);
        if (rear.speed > front.speed) rear.speed = front.speed * 0.98f;
      } else {
        float s = dx >= 0 ? 1.0f : -1.0f;
        A.x -= s * ox * 0.5f;
        C.x += s * ox * 0.5f;
      }
    }
  }
}
