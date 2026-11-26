/*
  ═══════════════════════════════════════════════════════════════
  PLAYER CAR PHYSICS — implementation
  All "per frame" factors are defined at 60 Hz and rescaled with
  powf(f, dt*60).
  ═══════════════════════════════════════════════════════════════
*/

#include "physics.h"
#include "track.h"
#include "race.h"
#include "rivals.h"
#include "render_scenery.h"
#include "utils.h"
#include "config.h"
#include <math.h>

// ═══════════════════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════════════════
float cameraDepth  = 1;
float playerZdist  = 0;
float position     = 0;
float playerX      = 0;
float speed        = 0;
float maxSpeed     = SEG_LEN * SPEED_MULTIPLIER;
float centrifugal  = CENTRIFUGAL;
bool  crashed      = false;
float crashTime    = 0;

float velocityX    = 0;
float acceleration = 0;
float driftAngle   = 0;
float steerVisual  = 0;
bool  braking      = false;
bool  boosting     = false;
float turboMeter   = 1;
bool  autopilot    = false;
float offRoad      = 0;

static inline float perFrame(float f, float dt) { return powf(f, dt * 60.0f); }

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void physics_placePlayer(float camPos, float x) {
  position     = camPos;
  playerX      = x;
  speed        = 0;
  velocityX    = 0;
  acceleration = 0;
  driftAngle   = 0;
  steerVisual  = 0;
  crashed      = false;
  crashTime    = 0;
  braking      = false;
  boosting     = false;
  turboMeter   = 1;
  offRoad      = 0;
}

void initPhysics() {
  float fovRad = FOV_DEG * (float)PI / 180.0f;
  cameraDepth  = 1.0f / tanf(fovRad / 2.0f);
  playerZdist  = CAM_HEIGHT * cameraDepth;
  maxSpeed     = SEG_LEN * SPEED_MULTIPLIER;
  physics_placePlayer(0, 0);
  autopilot = false;
}

// ═══════════════════════════════════════════════════════════════
//  INPUT / AUTOPILOT
// ═══════════════════════════════════════════════════════════════
void handleInput(float dt, const InputState& in) {
  const float playerZ = position + playerZdist;
  const float spPct = speed / maxSpeed;
  bool autoBrake = false;
  float steerCmd;

  if (autopilot) {
    // Racing line + corner speed (same helpers as the rivals), and a lane
    // change around any car ahead in the way.
    float target = ai_lineTarget(playerZ, 0.0f);
    float nearest = 1e9f;
    for (int i = 0; i < NUM_RACERS; i++) {
      if (i == PLAYER_ID) continue;
      const Racer& c = racers[i];
      float dz = wrapDelta(playerZ, c.z, trackLength);
      if (dz < -CAR_LEN * 0.6f || dz > CAR_LEN * 4.0f || dz > nearest) continue;
      if (fabsf(c.x - target) < CAR_HALF_W * 2.0f + 0.14f && (c.speed < speed || dz < CAR_LEN * 1.3f)) {
        nearest = dz;
        float left = c.x - 0.42f, right = c.x + 0.42f;
        target = ((fabsf(left - playerX) < fabsf(right - playerX) && left > -0.80f) || right > 0.80f)
                 ? left : right;
      }
    }
    target = clampF(target, -0.80f, 0.80f);
    float steer = (target - playerX) * STEER_AUTO * 1.6f * dt;
    playerX += steer;
    steerCmd = clampF(steer / (dt * STEER_MANUAL + 1e-4f), -1, 1);
    float vMax = ai_cornerSpeed(playerZ) * maxSpeed;
    if (racers[PLAYER_ID].finished) vMax = fminF(vMax, maxSpeed * 0.62f);   // cool-down
    autoBrake = speed > vMax + 400.0f;
  } else {
    // Steering authority grows with speed (no turning on the spot)
    float authority = 0.25f + 0.75f * clampF(spPct * 1.5f, 0, 1);
    playerX += in.steer * STEER_MANUAL * authority * dt;
    steerCmd = in.steer;
  }
  if (autopilot) playerX = clampF(playerX, -0.85f, 0.85f);

  // Throttle is automatic; the brake cuts it.
  braking = (in.brake && !autopilot) || autoBrake;
  boosting = !autopilot && in.turbo && !braking && turboMeter > 0.01f;
  if (boosting) turboMeter = fmaxF(0.0f, turboMeter - TURBO_DRAIN * dt);
  else          turboMeter = fminF(1.0f, turboMeter + TURBO_REFILL * dt);

  const float top = maxSpeed * (boosting ? 1.0f + TURBO_BOOST : 1.0f);
  const float targetAccel = maxSpeed * ACCEL_TARGET * (boosting ? TURBO_ACCEL : 1.0f);
  if (braking) {
    acceleration = 0;
    speed -= BRAKE_DECEL * dt;
    if (speed < 0) speed = 0;
  } else if (speed < top * ACCEL_NEAR_MAX) {
    acceleration += ACCEL_RAMP * (boosting ? TURBO_ACCEL : 1.0f) * dt;
    if (acceleration > targetAccel) acceleration = targetAccel;
  } else {
    acceleration *= perFrame(ACCEL_DAMPING, dt);
  }

  // Visual steering (car yaw) eases towards the command
  steerVisual += (steerCmd - steerVisual) * clampF(dt * 8.0f, 0, 1);
}

// ═══════════════════════════════════════════════════════════════
//  MOTION
// ═══════════════════════════════════════════════════════════════
// Per-60 Hz-frame speed factor when driving on a material off the road
static float materialDrag(int mat, float ax) {
  switch (mat) {
    case MAT_SIDEWALK: return 0.998f;                        // paved
    case MAT_SAND:     return ax > BEACH_WATER_X ? 0.95f : 0.976f;   // beach, then the sea
    case MAT_WATER:    return ax > SEAWALL_X ? 0.95f : 0.99f;
    case MAT_ROCK:     return 0.98f;
    case MAT_SNOW:     return 0.977f;
    case MAT_DIRT:     return 0.984f;
    default:           return 0.985f;                        // grass
  }
}

void updatePhysics(float dt) {
  if (segCount <= 0) return;
  const float playerZ = position + playerZdist;
  int pSeg = findSegIdx(playerZ);
  const Segment& seg = segments[pSeg];
  float spPct = speed / maxSpeed;

  // === ACCELERATION WITH FRICTION ===
  speed *= perFrame(FRICTION, dt);
  speed += acceleration * dt;

  // === GRAVITY: slope effect (acts on the speed, not on the throttle) ===
  float slope = (seg.y - segAt(pSeg - 1).y) / SEG_LEN;
  speed -= slope * GRAVITY_FACTOR * dt;

  // === OFF ROAD: drag per ground material ===
  float ax = fabsf(playerX);
  int mat = seg.isTunnel() ? MAT_SIDEWALK : seg.mat(playerX < 0 ? 0 : 1);
  if (ax > ROAD_EDGE_X + 0.05f) {
    float drag = materialDrag(mat, ax);
    if (speed > maxSpeed * 0.30f || mat == MAT_WATER || (mat == MAT_SAND && ax > BEACH_WATER_X))
      speed *= perFrame(drag, dt);
    offRoad = (mat == MAT_SIDEWALK) ? 0.0f : clampF((ax - 1.05f) * 4.0f, 0, 1);
  } else {
    offRoad = 0;
  }

  // Turbo raises the cap; above it the car eases back down
  const float top = maxSpeed * (boosting ? 1.0f + TURBO_BOOST : 1.0f);
  if (speed > top) speed -= (speed - top) * clampF(dt * 2.0f, 0, 1);
  if (speed < 0) speed = 0;

  // === DRIFT: lateral physics in curves ===
  float curveForce = seg.curve * centrifugal * spPct;
  velocityX += curveForce * dt * CURVE_FORCE;
  velocityX *= perFrame(LATERAL_FRICTION, dt);
  playerX -= velocityX * dt;

  float steerDx = dt * CENTRIFUGAL_DX * spPct;
  playerX -= steerDx * spPct * seg.curve * centrifugal;

  // === VISUAL DRIFT ANGLE ===
  if (speed > 0.1f) {
    driftAngle = atan2f(velocityX * 10.0f, speed / maxSpeed) * 0.5f;
    driftAngle = clampF(driftAngle, -0.5f, 0.5f);
  } else {
    driftAngle *= perFrame(0.9f, dt);
  }

  position = loopIncrease(position, dt * speed, trackLength);
}

// ═══════════════════════════════════════════════════════════════
//  COLLISIONS
// ═══════════════════════════════════════════════════════════════
static void crash() {
  if (crashed) return;
  crashed = true;
  crashTime = 0;
  boosting = false;
}

// Max |x| of the solid limit on one side of a segment (0 left, 1 right)
static float solidLimit(const Segment& s, int side) {
  if (s.isTunnel()) return TUNNEL_WALL_X;
  if (s.build[side]) return BUILDING_OFFSET;
  if (s.flags & (side ? SEGF_RAIL_R : SEGF_RAIL_L)) return RAIL_X;
  return OFFROAD_MAX_X;
}

void checkCollisions(float dt) {
  if (segCount <= 0 || crashed) return;
  const float playerZ = position + playerZdist;
  const int pSeg = findSegIdx(playerZ);
  // footprint: the car spans +-CAR_LEN/2 around its centre (~ +-2.75 segments)
  const int K0 = -(int)(CAR_LEN * 0.5f / SEG_LEN), K1 = (int)(CAR_LEN * 0.5f / SEG_LEN) + 1;

  // --- 1. Rivals: bumps, not crashes (unless the hit is very hard) ---
  for (int i = 0; i < NUM_RACERS; i++) {
    if (i == PLAYER_ID) continue;
    Racer& R = racers[i];
    float dz = wrapDelta(playerZ, R.z, trackLength);   // + : rival ahead
    float dx = R.x - playerX;
    if (fabsf(dz) >= CAR_LEN || fabsf(dx) >= CAR_HALF_W * 2.0f) continue;
    float ox = CAR_HALF_W * 2.0f - fabsf(dx);
    float oz = CAR_LEN - fabsf(dz);
    if (oz / CAR_LEN < ox / (CAR_HALF_W * 2.0f)) {
      // nose to tail
      float closing = dz > 0 ? speed - R.speed : R.speed - speed;
      if (closing > maxSpeed * 0.55f) crash();
      if (dz > 0) {          // the player rear-ends the rival
        float push = fmaxF(closing, 0.0f) * 0.25f;
        speed = fminF(speed, R.speed * 0.92f);
        R.speed = fminF(R.speed + push, maxSpeed * 1.05f);
        position = loopIncrease(position, -oz, trackLength);
      } else {               // the rival rear-ends the player
        float push = fmaxF(closing, 0.0f) * 0.25f;
        R.speed = fminF(R.speed, speed * 0.92f);
        speed = fminF(speed + push, maxSpeed * (1.0f + TURBO_BOOST));
        R.z = loopIncrease(R.z, -oz, trackLength);
      }
    } else {
      // side by side: push both apart, small speed loss
      float s = dx >= 0 ? -1.0f : 1.0f;     // direction the player moves
      playerX += s * ox * 0.55f;
      R.x -= s * ox * 0.45f;
      velocityX = 0;
      speed *= perFrame(0.985f, dt);
      R.speed *= perFrame(0.985f, dt);
    }
  }

  // --- 2. Solid limits: rails, tunnel walls, building / cliff faces ---
  for (int side = 0; side < 2; side++) {
    float lim = OFFROAD_MAX_X;
    for (int k = K0; k <= K1; k++) lim = fminF(lim, solidLimit(segAt(pSeg + k), side));
    float maxX = lim - CAR_HALF_W;
    float xs = side ? playerX : -playerX;          // outward offset on this side
    if (xs <= maxX) continue;
    float over = xs - maxX;
    if (over > 0.15f && speed > maxSpeed * 0.30f) {
      crash();                                      // drove into a wall end / portal
    } else {
      speed *= perFrame(0.975f, dt);               // scrape along it
    }
    playerX = side ? maxX : -maxX;
    velocityX = 0;
  }

  // --- 3. Solid scenery (trunks, rocks, posts) ---
  for (int k = K0; k <= K1 && !crashed; k++) {
    const int si = pSeg + k;
    const Segment& s = segAt(si);
    // sprites stand at the near boundary of their segment
    float dzs = wrapDelta(playerZ, (float)segWrap(si) * SEG_LEN, trackLength);
    if (fabsf(dzs) > CAR_LEN * 0.5f) continue;
    for (int side = 0; side < 2; side++) {
      if (s.spr[side] < 0) continue;
      float hw = spriteHitWidth(s.spr[side]) * 0.5f;
      if (hw <= 0) continue;
      float sx = s.sprX(side);
      float d = playerX - sx;
      float lim = hw + CAR_HALF_W;
      if (fabsf(d) >= lim) continue;
      if (dzs > CAR_LEN * 0.3f) {
        // at the front bumper: a real hit. Fast = crash; slow = knocked aside
        if (speed > maxSpeed * 0.25f) crash();
        else speed *= 0.3f;
      } else {
        speed *= perFrame(0.97f, dt);              // alongside: scrape past it
      }
      playerX = sx + (d >= 0 ? lim : -lim);        // out on the side it came from
      velocityX = 0;
    }
  }
}

void updateCrash(float dt) {
  if (!crashed) return;
  crashTime += dt;
  speed *= perFrame(0.90f, dt);
  if (crashTime >= CRASH_TIME) {
    crashed = false;
    crashTime = 0;
    speed = 0;
    acceleration = 0;
    velocityX = 0;
    driftAngle = 0;
    steerVisual = 0;
    playerX = clampF(playerX * 0.3f, -0.6f, 0.6f);   // back onto the road
  }
}
