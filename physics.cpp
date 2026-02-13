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
#include <math.h>

// ═══════════════════════════════════════════════════════════════
//  GLOBAL VARIABLES (Definition)
// ═══════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════
//  GAME STATE
// ═══════════════════════════════════════════════════════════════
GameState gameState    = STATE_COUNTDOWN;
unsigned long countdownStart = 0;
bool raceResultShown   = false;

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

  // Start in countdown state
  gameState      = STATE_COUNTDOWN;
  countdownStart = millis();
  raceResultShown = false;
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

#if MAX_CARS > 0
  // Update traffic (disabled in race mode)
  for (int i = 0; i < MAX_CARS; i++) {
    trafficCars[i].z = loopIncrease(trafficCars[i].z, dt * trafficCars[i].speed, trackLength);
    if (random(0, 200) < 2) {
      trafficCars[i].offset += (random(-1, 2)) * 0.1;
      trafficCars[i].offset  = clampF(trafficCars[i].offset, -0.8, 0.8);
    }
  }
#endif
}

void checkCollisions() {
  const float playerW = 0.15;
  int pSeg = findSegIdx(position + playerZdist);
  Segment& s = segments[pSeg];

#if MAX_CARS > 0
  // Collisions with traffic (disabled in race mode)
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
#endif

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

// ═══════════════════════════════════════════════════════════════
//  AI COMPETITOR UPDATE
// ═══════════════════════════════════════════════════════════════
void updateCompetitors(float dt) {
  for (int i = 0; i < NUM_COMPETITORS; i++) {
    CompetitorCar& c = competitors[i];
    float compMax = maxSpeed * c.speedFactor;

    // Progressive acceleration (same logic as player)
    float targetAccel = compMax * ACCEL_TARGET;
    if (c.speed < compMax * ACCEL_NEAR_MAX) {
      c.acceleration += ACCEL_RAMP * dt;
      if (c.acceleration > targetAccel) c.acceleration = targetAccel;
    } else {
      c.acceleration *= ACCEL_DAMPING;
    }

    // Gravity / slope effect
    int pSeg = findSegIdx(c.z);
    int prevSeg = (pSeg - 1 + TOTAL_SEGS) % TOTAL_SEGS;
    float slope = (segments[pSeg].y - segments[prevSeg].y) / SEG_LEN;
    c.acceleration += -slope * GRAVITY_FACTOR * dt;

    // Friction + apply acceleration
    c.speed *= FRICTION;
    c.speed += c.acceleration * dt;
    if (c.speed > compMax) c.speed = compMax;
    if (c.speed < 0) c.speed = 0;

    float spPct = c.speed / maxSpeed;

    // Counter-steer based on upcoming curve
    float curCurve = segments[pSeg].curve;
    float target = -curCurve * 0.12f;
    float steer = (target - c.x) * STEER_AUTO * c.steerFactor * dt;
    c.x += steer;

    // Centrifugal drift
    float curveForce = curCurve * CENTRIFUGAL * spPct;
    c.velocityX += curveForce * dt * CURVE_FORCE;
    c.velocityX *= LATERAL_FRICTION;
    c.x -= c.velocityX * dt;

    // Additional centrifugal push
    float steerDx = dt * CENTRIFUGAL_DX * spPct;
    c.x -= steerDx * spPct * curCurve * CENTRIFUGAL;

    c.x = clampF(c.x, -0.8f, 0.8f);

    // Visual drift angle
    if (c.speed > 0.1f) {
      c.driftAngle = atan2f(c.velocityX * 10.0f, c.speed / maxSpeed) * 0.5f;
      c.driftAngle = clampF(c.driftAngle, -0.5f, 0.5f);
    } else {
      c.driftAngle *= 0.9f;
    }

    // Advance position
    float prevZ = c.z;
    c.z = loopIncrease(c.z, dt * c.speed, trackLength);

    // Lap detection (same as player)
    if (c.z < prevZ && prevZ > trackLength * 0.9f) {
      if (c.lap < totalLaps) {
        c.lap++;
      }
    }
  }

  // Check collisions between competitors
  for (int i = 0; i < NUM_COMPETITORS; i++) {
    for (int j = i + 1; j < NUM_COMPETITORS; j++) {
      // Calculate relative position
      float dx = competitors[i].x - competitors[j].x;
      float dz = competitors[i].z - competitors[j].z;

      // Handle track wrapping (if cars are on opposite sides of start/finish)
      if (dz > trackLength / 2) dz -= trackLength;
      if (dz < -trackLength / 2) dz += trackLength;

      // Check collision distance
      // Lateral: ~0.3 units, longitudinal: ~1.5 segments = 300 units
      const float MIN_LATERAL = 0.3f;
      const float MIN_LONGITUDINAL = 300.0f;

      if (fabsf(dx) < MIN_LATERAL && fabsf(dz) < MIN_LONGITUDINAL) {
        // Collision detected — apply separation force

        // Lateral separation (push apart sideways)
        float pushX = (dx > 0 ? 0.5f : -0.5f) * dt * 2.0f;
        competitors[i].x += pushX;
        competitors[j].x -= pushX;

        // Longitudinal separation (slow down the car behind)
        if (dz > 0) {
          // i is ahead, slow down j
          competitors[j].speed *= 0.95f;
        } else {
          // j is ahead, slow down i
          competitors[i].speed *= 0.95f;
        }

        // Clamp positions
        competitors[i].x = clampF(competitors[i].x, -0.8f, 0.8f);
        competitors[j].x = clampF(competitors[j].x, -0.8f, 0.8f);
      }
    }
  }
}
