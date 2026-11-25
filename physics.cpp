/*
  ═══════════════════════════════════════════════════════════════
  PHYSICS AND LOGIC IMPLEMENTATION
  All "per frame" factors from config.h are defined at 60 Hz and are
  rescaled with powf(f, dt*60): the old code applied them once per
  frame, so the handling changed with the frame rate.
  ═══════════════════════════════════════════════════════════════
*/

#include "physics.h"
#include "track.h"
#include "utils.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

// ═══════════════════════════════════════════════════════════════
//  GLOBAL VARIABLES (Definition)
// ═══════════════════════════════════════════════════════════════
float cameraDepth;
float playerZdist;
float position   = 0;
float playerX    = 0;
float speed      = 0;
float maxSpeed;
float centrifugal = CENTRIFUGAL;

bool  crashed     = false;
unsigned long crashTimer = 0;
unsigned long lastFrameMs;

float currentLapTime = 0;
float lastLapTime    = 0;
float bestLapTime    = 0;
float prevPosition   = 0;
int currentLap       = 1;
int totalLaps        = TOTAL_LAPS;

float velocityX      = 0;
float acceleration   = 0;
float driftAngle     = 0;
float steerVisual    = 0;
bool  braking        = false;
bool  autopilot      = true;
float offRoad        = 0;

static unsigned long lastInputMs = 0;

static inline float perFrame(float f, float dt) { return powf(f, dt * 60.0f); }

// ═══════════════════════════════════════════════════════════════
//  IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

void initPhysics() {
  float fovRad = FOV_DEG * (float)PI / 180.0f;
  cameraDepth  = 1.0f / tanf(fovRad / 2.0f);
  playerZdist  = CAM_HEIGHT * cameraDepth;
  maxSpeed     = SEG_LEN * SPEED_MULTIPLIER;

  position       = 0;
  playerX        = 0;
  speed          = 0;
  crashed        = false;
  currentLapTime = 0;
  lastLapTime    = 0;
  bestLapTime    = 0;
  prevPosition   = 0;
  currentLap     = 1;

  velocityX    = 0;
  acceleration = 0;
  driftAngle   = 0;
  steerVisual  = 0;
  autopilot    = true;
  lastInputMs  = 0;
}

void handleInput(float dt, const InputState& in) {
  unsigned long now = millis();
  if (in.any) { lastInputMs = now; autopilot = false; }
  else if (!autopilot && now - lastInputMs > AUTOPILOT_IDLE_MS) autopilot = true;

  // Throttle is automatic (as designed); the brake button cuts it.
  braking = in.brake && !autopilot;
  float targetAccel = maxSpeed * ACCEL_TARGET;
  if (braking) {
    acceleration = 0;
    speed -= BRAKE_DECEL * dt;
    if (speed < 0) speed = 0;
  } else if (speed < maxSpeed * ACCEL_NEAR_MAX) {
    acceleration += ACCEL_RAMP * dt;
    if (acceleration > targetAccel) acceleration = targetAccel;
  } else {
    acceleration *= perFrame(ACCEL_DAMPING, dt);
  }

  int pSeg = findSegIdx(position + playerZdist);
  float curCurve = segments[pSeg].curve;
  float spPct = speed / maxSpeed;
  float steerCmd;

  if (autopilot) {
    // Counter-steer according to the curve, and change lane around any
    // slower car ahead (the old autopilot drove straight into traffic).
    float target = -curCurve * 0.12f;
    float playerZ = position + playerZdist;
    float nearest = 1e9f;
    for (int i = 0; i < MAX_CARS; i++) {
      const TrafficCar& c = trafficCars[i];
      float dz = c.z - playerZ;
      if (dz < -trackLength * 0.5f) dz += trackLength;
      if (dz > trackLength * 0.5f) dz -= trackLength;
      if (dz < -SEG_LEN || dz > SEG_LEN * 14 || dz > nearest) continue;
      if (fabsf(c.offset - target) < 0.5f && c.speed < speed) {
        nearest = dz;
        // pass on the side with more room
        float left = c.offset - 0.62f, right = c.offset + 0.62f;
        target = (fabsf(left - playerX) < fabsf(right - playerX) && left > -0.85f) || right > 0.85f
                 ? left : right;
      }
    }
    target = clampF(target, -0.85f, 0.85f);
    float steer = (target - playerX) * STEER_AUTO * 1.6f * dt;
    playerX += steer;
    steerCmd = clampF(steer / (dt * STEER_MANUAL + 1e-4f), -1, 1);
  } else {
    // Steering authority grows with speed (no turning on the spot)
    float authority = 0.25f + 0.75f * clampF(spPct * 1.5f, 0, 1);
    playerX += in.steer * STEER_MANUAL * authority * dt;
    steerCmd = in.steer;
  }
  playerX = clampF(playerX, -2.5f, 2.5f);
  if (autopilot) playerX = clampF(playerX, -0.8f, 0.8f);

  // Visual steering (car yaw) eases towards the command
  steerVisual += (steerCmd - steerVisual) * clampF(dt * 8.0f, 0, 1);
}

void updatePhysics(float dt) {
  int pSeg = findSegIdx(position + playerZdist);
  int prevSegIdx = (pSeg - 1 + TOTAL_SEGS) % TOTAL_SEGS;
  float spPct = speed / maxSpeed;

  // === ACCELERATION WITH FRICTION ===
  speed *= perFrame(FRICTION, dt);
  speed += acceleration * dt;

  // === GRAVITY: slope effect (hills/dips) ===
  // Acts on the speed. It used to be added to `acceleration` (the throttle
  // state) every frame, so a long climb drained the throttle below zero and
  // the car crawled to a stop on every big hill.
  float slope = (segments[pSeg].y - segments[prevSegIdx].y) / SEG_LEN;
  speed -= slope * GRAVITY_FACTOR * dt;

  // Grass slows the car down (it used to be free until |x| > 2.4)
  float off = fabsf(playerX) - 1.05f;
  offRoad = clampF(off * 4.0f, 0, 1);
  if (offRoad > 0 && speed > maxSpeed * 0.35f) speed *= perFrame(0.985f, dt);

  if (speed > maxSpeed) speed = maxSpeed;
  if (speed < 0) speed = 0;

  // === DRIFT: lateral physics in curves ===
  float curveForce = segments[pSeg].curve * centrifugal * spPct;
  velocityX += curveForce * dt * CURVE_FORCE;
  velocityX *= perFrame(LATERAL_FRICTION, dt);
  playerX -= velocityX * dt;

  float steerDx = dt * CENTRIFUGAL_DX * spPct;
  playerX -= steerDx * spPct * segments[pSeg].curve * centrifugal;

  // === VISUAL DRIFT ANGLE ===
  if (speed > 0.1f) {
    driftAngle = atan2f(velocityX * 10.0f, speed / maxSpeed) * 0.5f;
    driftAngle = clampF(driftAngle, -0.5f, 0.5f);
  } else {
    driftAngle *= perFrame(0.9f, dt);
  }

  prevPosition = position;
  position = loopIncrease(position, dt * speed, trackLength);

  // Detect lap completion
  if (position < prevPosition && prevPosition > trackLength * 0.9f) {
    if (currentLapTime > 5.0f) {
      lastLapTime = currentLapTime;
      if (bestLapTime <= 0 || currentLapTime < bestLapTime)
        bestLapTime = currentLapTime;
      currentLap = (currentLap < totalLaps) ? currentLap + 1 : 1;
    }
    currentLapTime = 0;
  }
  currentLapTime += dt;

  // === TRAFFIC: keep lanes, change lane now and then, ease across ===
  for (int i = 0; i < MAX_CARS; i++) {
    TrafficCar& c = trafficCars[i];
    c.z = loopIncrease(c.z, dt * c.speed, trackLength);
    if (random(0, 1000) < (int)(4 * dt * 60)) {
      int lane = random(0, LANES);
      c.targetOffset = (lane - 1) * (2.0f / LANES);
    }
    c.offset += (c.targetOffset - c.offset) * clampF(dt * 1.2f, 0, 1);
  }
}

static void crash() {
  crashed = true;
  crashTimer = millis();
}

void checkCollisions() {
  const float playerW = 0.30f;
  int pSeg = findSegIdx(position + playerZdist);
  Segment& s = segments[pSeg];

  // Traffic: compare positions along the track (with wrap) instead of
  // segment indices, which missed hits across the lap line.
  float playerZ = position + playerZdist;
  for (int i = 0; i < MAX_CARS; i++) {
    TrafficCar& c = trafficCars[i];
    float dz = c.z - playerZ;
    if (dz >  trackLength * 0.5f) dz -= trackLength;
    if (dz < -trackLength * 0.5f) dz += trackLength;
    if (dz < -SEG_LEN * 0.5f || dz > SEG_LEN * 1.5f) continue;
    if (speed > c.speed && overlapChk(playerX, playerW, c.offset, 0.30f)) {
      float before = speed;
      speed = c.speed * 0.7f;
      position = loopIncrease(position, -(speed * 0.05f), trackLength);
      if (before > maxSpeed * 0.5f) crash();
    }
  }

  // Tunnel walls
  if (s.tunnel) {
    const float wallX = 0.95f;
    if (playerX < -wallX || playerX > wallX) {
      playerX = clampF(playerX, -wallX, wallX);
      velocityX = 0.0f;
      if (speed > maxSpeed * 0.35f) crash();
      speed *= 0.3f;
    }
  }

  // Scenery sprites (only reachable off the road)
  if (playerX < -1.0f || playerX > 1.0f) {
    float off = s.spriteOffset();
    if (s.spriteType >= 0 && s.spriteType != SPR_BUSH && overlapChk(playerX, playerW, off, 0.4f)) {
      if (speed > maxSpeed * 0.25f) crash();
      speed *= 0.2f;
    }
  }

  // Going completely off the road
  if (playerX <= -2.4f || playerX >= 2.4f) {
    if (speed > maxSpeed * 0.8f) crash();   // same threshold as before (0.4 after halving)
    speed *= 0.5f;
  }
}
