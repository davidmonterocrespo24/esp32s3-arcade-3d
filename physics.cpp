/*
  ═══════════════════════════════════════════════════════════════
  PHYSICS AND LOGIC IMPLEMENTATION
  ═══════════════════════════════════════════════════════════════
*/

#include "physics.h"
#include "track.h"
#include "utils.h"
#include "config.h"
#include <Arduino.h>

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
int totalLaps        = 3;

// Advanced physics variables
float velocityX      = 0;     // Lateral velocity (for drift)
float acceleration   = 0;     // Current acceleration
float driftAngle     = 0;     // Drift angle

// ═══════════════════════════════════════════════════════════════
//  IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════

void initPhysics() {
  float fovRad = FOV_DEG * PI / 180.0;
  cameraDepth  = 1.0 / tanf(fovRad / 2.0);
  playerZdist  = CAM_HEIGHT * cameraDepth;
  maxSpeed     = SEG_LEN * SPEED_MULTIPLIER;

  position     = 0;
  playerX      = 0;
  speed        = 0;
  crashed      = false;
  currentLapTime = 0;
  lastLapTime    = 0;
  bestLapTime    = 0;
  prevPosition   = 0;

  // Initialize advanced physics
  velocityX    = 0;
  acceleration = 0;
  driftAngle   = 0;
}

void handleInput(float dt) {
  // === DEMO MODE: Auto-pilot with progressive acceleration ===

  // Progressive acceleration (not instantaneous)
  float targetAccel = maxSpeed * ACCEL_TARGET;
  float accelSpeed  = ACCEL_RAMP;

  if (speed < maxSpeed * ACCEL_NEAR_MAX) {
    acceleration += accelSpeed * dt;
    if (acceleration > targetAccel) acceleration = targetAccel;
  } else {
    acceleration *= ACCEL_DAMPING;
  }

  // Read current curve to anticipate the turn
  int pSeg = findSegIdx(position + playerZdist);
  float curCurve = segments[pSeg].curve;

  // Counter-steer according to curve + return to center
  float target = -curCurve * 0.12;
  float steer = (target - playerX) * STEER_AUTO * dt;
  playerX += steer;

  playerX = clampF(playerX, -0.8, 0.8);
}

void updatePhysics(float dt) {
  int pSeg = findSegIdx(position + playerZdist);
  int prevSegIdx = (pSeg - 1 + TOTAL_SEGS) % TOTAL_SEGS;

  float spPct = speed / maxSpeed;

  // === GRAVITY: Slope effect (hills/dips) ===
  float currentY = segments[pSeg].y;
  float prevY = segments[prevSegIdx].y;
  float slope = (currentY - prevY) / SEG_LEN;  // Terrain slope

  // Going up = slower, going down = faster
  float gravityEffect = -slope * GRAVITY_FACTOR;
  acceleration += gravityEffect * dt;

  // === ACCELERATION WITH FRICTION ===
  // Air and ground friction
  float friction = FRICTION;
  speed *= friction;

  // Apply acceleration to speed
  speed += acceleration * dt;

  // Limit speed
  if (speed > maxSpeed) speed = maxSpeed;
  if (speed < 0) speed = 0;

  // === DRIFT: Lateral physics in curves ===
  float curveForce = segments[pSeg].curve * centrifugal * spPct;

  // Lateral velocity increases with curve force
  velocityX += curveForce * dt * CURVE_FORCE;

  // Lateral friction (car tries to return to center)
  velocityX *= LATERAL_FRICTION;

  // Apply lateral velocity to position
  playerX -= velocityX * dt;

  // Also apply curve push (centrifugal)
  float steerDx = dt * CENTRIFUGAL_DX * spPct;
  playerX -= steerDx * spPct * segments[pSeg].curve * centrifugal;

  // === VISUAL DRIFT ANGLE ===
  // Calculate angle based on lateral velocity and forward speed
  if (speed > 0.1f) {
    driftAngle = atan2f(velocityX * 10.0f, speed / maxSpeed) * 0.5f;
    driftAngle = clampF(driftAngle, -0.5f, 0.5f);
  } else {
    driftAngle *= 0.9f;  // Gradually reduce angle
  }

  prevPosition = position;
  position = loopIncrease(position, dt * speed, trackLength);

  // Detect lap completion
  if (position < prevPosition && prevPosition > trackLength * 0.9) {
    if (currentLapTime > 5.0) {
      lastLapTime = currentLapTime;
      if (bestLapTime <= 0 || currentLapTime < bestLapTime)
        bestLapTime = currentLapTime;

      // Advance to next lap
      if (currentLap < totalLaps) {
        currentLap++;
      } else {
        // Race finished! Reset
        currentLap = 1;
      }
    }
    currentLapTime = 0;
  }
  currentLapTime += dt;

  // Update traffic
  for (int i = 0; i < MAX_CARS; i++) {
    trafficCars[i].z = loopIncrease(trafficCars[i].z, dt * trafficCars[i].speed, trackLength);
    if (random(0, 200) < 2) {
      trafficCars[i].offset += (random(-1, 2)) * 0.1;
      trafficCars[i].offset  = clampF(trafficCars[i].offset, -0.8, 0.8);
    }
  }
}

void checkCollisions() {
  const float playerW = 0.15;
  int pSeg = findSegIdx(position + playerZdist);
  Segment& s = segments[pSeg];

  // Collisions with traffic
  for (int i = 0; i < MAX_CARS; i++) {
    int cs = findSegIdx(trafficCars[i].z);
    int d  = abs(cs - pSeg);
    if (d > 3 && d < TOTAL_SEGS - 3) continue;
    if (speed > trafficCars[i].speed && overlapChk(playerX, playerW, trafficCars[i].offset, 0.15)) {
      speed = trafficCars[i].speed * 0.7;
      position = loopIncrease(position, -(speed * 0.05), trackLength);
      if (speed > maxSpeed * 0.5) {
        crashed = true;
        crashTimer = millis();
      }
    }
  }

  // Collisions with tunnel walls
  if (s.tunnel) {
    const float wallX = 0.95f;
    if (playerX < -wallX || playerX > wallX) {
      playerX = clampF(playerX, -wallX, wallX);
      velocityX = 0.0f;
      speed *= 0.3f;
      if (speed > maxSpeed * 0.35f) {
        crashed = true;
        crashTimer = millis();
      }
    }
  }

  // Collisions with sprites
  if (playerX < -1.0 || playerX > 1.0) {
    if (s.spriteType >= 0 && overlapChk(playerX, playerW, s.spriteOffset, 0.4)) {
      speed *= 0.2;
      if (speed > maxSpeed * 0.25) {
        crashed = true;
        crashTimer = millis();
      }
    }
  }

  // Going completely off the road
  if (playerX <= -2.4 || playerX >= 2.4) {
    speed *= 0.5;
    if (speed > maxSpeed * 0.4) {
      crashed = true;
      crashTimer = millis();
    }
  }
}
