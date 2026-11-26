/*
  ═══════════════════════════════════════════════════════════════
  PLAYER CAR PHYSICS — implementation
  All "per frame" factors are defined at 60 Hz and rescaled with
  powf(f, dt*60).

  Frame order (game.cpp): handleInput -> updatePhysics -> checkCollisions
  (walls, rails, tunnels, building faces, scenery, water) ->
  rivals_update, which moves the rivals and then calls
  physics_resolveContacts() so every car-to-car bump is resolved with
  all cars at the same time step (resolving them before the rivals
  moved compared positions one frame apart: ~400 units at full speed).
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
float rocketTime   = 0;
bool  rocketStarted = false;
float bumpShake    = 0;
float draft        = 0;

static bool sTurboPrev  = false;   // turbo input of the previous frame (press edges)
static bool sFirstInput = true;    // first handleInput of this race (input held before GO)
static float sKickVX    = 0;       // part of velocityX that came from a contact bounce

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
  rocketTime   = 0;
  rocketStarted = false;
  bumpShake    = 0;
  draft        = 0;
  sTurboPrev   = false;
  sFirstInput  = true;
  sKickVX      = 0;
}

float physics_cruiseAccel(float top) {
  // friction removes speed * (1 - FRICTION^60) per second
  return top * ACCEL_NEAR_MAX * (1.0f - powf(FRICTION, 60.0f));
}

float physics_draftOf(int i) {
  if (i < 0 || i >= NUM_RACERS || trackLength <= 0) return 0.0f;
  const Racer& me = racers[i];
  if (me.speed < maxSpeed * 0.5f) return 0.0f;      // only at speed
  float best = 0.0f;
  for (int j = 0; j < NUM_RACERS; j++) {
    if (j == i) continue;
    const Racer& o = racers[j];
    float gap = wrapDelta(me.z, o.z, trackLength) - CAR_LEN;   // bumper to bumper
    if (gap < -CAR_LEN * 0.1f || gap > DRAFT_RANGE) continue;
    if (fabsf(o.x - me.x) >= DRAFT_WIDTH) continue;
    best = fmaxF(best, 1.0f - fmaxF(gap, 0.0f) / DRAFT_RANGE);
  }
  return best;
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
  const float authority = 0.25f + 0.75f * clampF(spPct * 1.5f, 0, 1);   // no turning on the spot
  bool brakeIn, turboIn;
  float steerCmd;

  if (autopilot) {
    // The rival controller drives (same line, speeds and traffic logic). It
    // asks for a lateral velocity; steer for it, compensating the drift that
    // updatePhysics is about to add, within a human's steering authority.
    AiCmd cmd;
    ai_drivePlayer(dt, cmd);
    const Segment& seg = segAt(findSegIdx(playerZ));
    // (only the curve's drift: a bounce from a contact is left to act)
    float vxNext = (velocityX - sKickVX + seg.curve * centrifugal * spPct * dt * CURVE_FORCE) *
                   perFrame(LATERAL_FRICTION, dt);
    float drift = -(vxNext * dt + dt * CENTRIFUGAL_DX * spPct * spPct * seg.curve * centrifugal);
    float maxDx = STEER_MANUAL * authority * dt;
    float dx = clampF(cmd.xVel * dt - drift, -maxDx, maxDx);
    playerX += dx;
    steerCmd = maxDx > 1e-6f ? dx / maxDx : 0.0f;
    brakeIn = cmd.brake;
    turboIn = cmd.turbo;
  } else {
    playerX += in.steer * STEER_MANUAL * authority * dt;
    steerCmd = in.steer;
    brakeIn = in.brake;
    turboIn = in.turbo;
  }

  // Rocket start: a NEW turbo press within ROCKET_WINDOW of GO. The first
  // frame of the race only records the input, so holding TURBO through the
  // countdown does not count.
  if (sFirstInput) { sTurboPrev = turboIn; sFirstInput = false; }
  const bool turboPress = turboIn && !sTurboPrev;
  sTurboPrev = turboIn;
  if (turboPress && raceRunning && raceTime <= ROCKET_WINDOW && !rocketStarted && speed < maxSpeed * 0.1f) {
    rocketStarted = true;
    rocketTime = ROCKET_TIME;
    if (acceleration < ROCKET_ACCEL) acceleration = ROCKET_ACCEL;
  }
  if (rocketTime > 0) rocketTime = fmaxF(0.0f, rocketTime - dt);

  // Throttle is automatic; the brake overrides it.
  braking = brakeIn;
  boosting = ((turboIn && !braking && turboMeter > 0.01f) || rocketTime > 0) && !crashed;
  if (boosting && rocketTime <= 0) turboMeter = fmaxF(0.0f, turboMeter - TURBO_DRAIN * dt);
  else if (!boosting)              turboMeter = fminF(1.0f, turboMeter + TURBO_REFILL * dt);

  // Slipstream behind a car in the same lane (racers[] mirror of last frame)
  draft += (physics_draftOf(PLAYER_ID) - draft) * clampF(dt * 2.0f, 0, 1);

  const float tmul = boosting ? TURBO_ACCEL : 1.0f;
  const float top = maxSpeed * (boosting ? 1.0f + TURBO_BOOST : 1.0f) * (1.0f + DRAFT_BOOST * draft);
  const float targetAccel = maxSpeed * ACCEL_TARGET * tmul;
  if (braking) {
    // the engine's push fades while braking (a short tap costs little)
    acceleration *= perFrame(BRAKE_ACCEL_KEEP, dt);
    speed -= BRAKE_DECEL * dt;
    if (speed < 0) speed = 0;
  } else if (speed < top * ACCEL_NEAR_MAX) {
    acceleration += ACCEL_RAMP * tmul * dt;
    if (acceleration > targetAccel) acceleration = targetAccel;
  } else {
    // above the cruise point (turbo just ended, slipstream, downhill): ease
    // off, but only down to the throttle that holds the normal cruise speed
    // against friction. Decaying to zero made the car sag far below the
    // cruise afterwards and re-ramp for seconds at ACCEL_RAMP.
    acceleration = fmaxF(acceleration * perFrame(ACCEL_DAMPING, dt), fminF(acceleration, physics_cruiseAccel(maxSpeed)));
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

// Off-road drag for any car at |x| = ax on material mat (speed factor this frame)
static float offRoadFactor(const Segment& seg, float x, float spd, float dt) {
  float ax = fabsf(x);
  if (ax <= ROAD_EDGE_X + 0.05f) return 1.0f;
  int mat = seg.isTunnel() ? MAT_SIDEWALK : seg.mat(x < 0 ? 0 : 1);
  if (spd > maxSpeed * 0.30f || mat == MAT_WATER || (mat == MAT_SAND && ax > BEACH_WATER_X))
    return perFrame(materialDrag(mat, ax), dt);
  return 1.0f;
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
  speed *= offRoadFactor(seg, playerX, speed, dt);
  {
    float ax = fabsf(playerX);
    int mat = seg.isTunnel() ? MAT_SIDEWALK : seg.mat(playerX < 0 ? 0 : 1);
    offRoad = (ax <= ROAD_EDGE_X + 0.05f || mat == MAT_SIDEWALK) ? 0.0f : clampF((ax - 1.05f) * 4.0f, 0, 1);
  }

  // Turbo and the slipstream raise the cap; above it the car eases back down
  const float top = maxSpeed * (boosting ? 1.0f + TURBO_BOOST : 1.0f) * (1.0f + DRAFT_BOOST * draft);
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

  bumpShake *= perFrame(0.88f, dt);
  sKickVX *= perFrame(LATERAL_FRICTION, dt);
  position = loopIncrease(position, dt * speed, trackLength);
}

// ═══════════════════════════════════════════════════════════════
//  SOLID LIMITS
// ═══════════════════════════════════════════════════════════════
static void crash() {
  if (crashed) return;
  crashed = true;
  crashTime = 0;
  boosting = false;
  rocketTime = 0;
}

// Max |x| of the solid limit on one side of a segment (0 left, 1 right)
static float solidLimit(const Segment& s, int side) {
  if (s.isTunnel()) return TUNNEL_WALL_X;
  if (s.build[side]) return BUILDING_OFFSET;
  if (s.flags & (side ? SEGF_RAIL_R : SEGF_RAIL_L)) return RAIL_X;
  return OFFROAD_MAX_X;
}

float physics_maxX(float z, int side) {
  if (segCount <= 0) return OFFROAD_MAX_X - CAR_HALF_W;
  // every segment under the footprint, rear bumper to front bumper
  const int s0 = findSegIdx(z - CAR_LEN * 0.5f);
  const int count = (int)(CAR_LEN / SEG_LEN) + 2;
  float lim = OFFROAD_MAX_X;
  for (int k = 0; k < count; k++) lim = fminF(lim, solidLimit(segAt(s0 + k), side));
  return lim - CAR_HALF_W;
}

// Keep the player inside the solid limits. A limit that appears well inside
// the car at speed is a frontal hit (a wall end, a portal): crash.
static void playerWalls(float dt, bool allowCrash) {
  const float pz = position + playerZdist;
  for (int side = 0; side < 2; side++) {
    float maxX = physics_maxX(pz, side);
    float xs = side ? playerX : -playerX;            // outward offset on this side
    if (xs <= maxX) continue;
    float over = xs - maxX;
    if (allowCrash && over > 0.15f && speed > maxSpeed * 0.30f) crash();
    else speed *= perFrame(0.975f, dt);              // scrape along it
    playerX = side ? maxX : -maxX;
    velocityX = 0;
  }
}

// Same limits for a rival (pushed there by a bump): it scrapes, never crashes
static void rivalWalls(int i, float dt) {
  Racer& R = racers[i];
  for (int side = 0; side < 2; side++) {
    float maxX = physics_maxX(R.z, side);
    float xs = side ? R.x : -R.x;
    if (xs <= maxX) continue;
    R.x = side ? maxX : -maxX;
    R.speed *= perFrame(0.975f, dt);
    float v = ai_latVel(i);
    if ((side && v > 0) || (!side && v < 0)) ai_kickLatVel(i, -v);   // stop moving into it
  }
  if (fabsf(R.x) > ROAD_EDGE_X + 0.05f)
    R.speed *= offRoadFactor(segAt(findSegIdx(R.z)), R.x, R.speed, dt);
}

void checkCollisions(float dt) {
  if (segCount <= 0 || crashed) return;
  const float playerZ = position + playerZdist;
  const int pSeg = findSegIdx(playerZ);

  // --- 1. Solid limits: rails, tunnel walls, building / cliff faces ---
  playerWalls(dt, true);

  // --- 2. Water: never reachable on the designed circuits (rails), but a car
  //        that ends up fully in the sea is reset like a crash ---
  {
    const Segment& s = segments[pSeg];
    const int side = playerX < 0 ? 0 : 1;
    const int mat = s.isTunnel() ? MAT_GRASS : s.mat(side);
    const float ax = fabsf(playerX);
    if ((mat == MAT_WATER && ax > SEAWALL_X + CAR_HALF_W) ||
        (mat == MAT_SAND && ax > BEACH_WATER_X + CAR_HALF_W))
      crash();
  }

  // --- 3. Solid scenery (trunks, rocks, posts) ---
  const int K0 = -(int)(CAR_LEN * 0.5f / SEG_LEN), K1 = (int)(CAR_LEN * 0.5f / SEG_LEN) + 1;
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

// ═══════════════════════════════════════════════════════════════
//  CAR-TO-CAR CONTACTS (player and rivals, same rules for every pair)
// ═══════════════════════════════════════════════════════════════
struct Body { float z, x, speed, xVel; };

static void getBody(int i, Body& b) {
  if (i == PLAYER_ID) {
    b.z = loopIncrease(position + playerZdist, 0, trackLength);
    b.x = playerX;
    b.speed = speed;
    b.xVel = -velocityX;                 // updatePhysics: playerX -= velocityX * dt
  } else {
    const Racer& R = racers[i];
    b.z = R.z;
    b.x = R.x;
    b.speed = R.speed;
    b.xVel = ai_latVel(i);
  }
}

static void putBody(int i, const Body& b, const Body& old) {
  if (i == PLAYER_ID) {
    position = loopIncrease(position, wrapDelta(old.z, b.z, trackLength), trackLength);
    playerX = b.x;
    speed = b.speed;
    sKickVX += -b.xVel - velocityX;
    velocityX = -b.xVel;
    Racer& P = racers[PLAYER_ID];
    P.z = loopIncrease(b.z, 0, trackLength);
    P.x = b.x;
    P.speed = b.speed;
  } else {
    Racer& R = racers[i];
    R.z = loopIncrease(b.z, 0, trackLength);
    R.x = b.x;
    R.speed = b.speed;
    ai_kickLatVel(i, b.xVel - old.xVel);
  }
}

static void pairContact(int ia, int ib, float dt) {
  Body A, B;
  getBody(ia, A);
  getBody(ib, B);
  const float dz = wrapDelta(A.z, B.z, trackLength);   // + : B ahead of A
  const float dx = B.x - A.x;                           // + : B right of A
  const float W2 = 2.0f * CAR_HALF_W;
  if (fabsf(dz) >= CAR_LEN || fabsf(dx) >= W2) return;
  const Body A0 = A, B0 = B;
  const float ox = W2 - fabsf(dx);                      // lateral overlap (half-widths)
  const float oz = CAR_LEN - fabsf(dz);                 // longitudinal overlap (world units)

  if (oz < ox * ROAD_W) {
    // ---- nose to tail: separate along the track ----
    Body& rear  = dz > 0 ? A : B;
    Body& front = dz > 0 ? B : A;
    const int rearId = dz > 0 ? ia : ib;
    const float closing = rear.speed - front.speed;
    if (closing > 0) {
      if (rearId == PLAYER_ID && closing > maxSpeed * BUMP_CRASH) crash();   // very hard hit
      // equal masses with restitution: the rear car loses speed, the front
      // car gets a smaller push (part of the energy goes into the bump)
      const float J = closing * (1.0f + BUMP_REST) * 0.5f;
      rear.speed = fmaxF(0.0f, rear.speed - J) * 0.99f;
      front.speed += J * 0.75f;
    }
    rear.z  -= oz * 0.6f + 1.0f;
    front.z += oz * 0.4f;
  } else {
    // ---- side by side: push apart, bounce, small speed loss ----
    float s = dx > 0 ? 1.0f : (dx < 0 ? -1.0f : (ia < ib ? 1.0f : -1.0f));
    const float push = ox * 0.5f + 0.003f;
    A.x -= s * push;
    B.x += s * push;
    if (A.xVel * s > -SIDE_KICK) A.xVel = -s * SIDE_KICK;
    if (B.xVel * s <  SIDE_KICK) B.xVel =  s * SIDE_KICK;
    A.speed *= perFrame(SIDE_RUB_KEEP, dt);
    B.speed *= perFrame(SIDE_RUB_KEEP, dt);
  }
  putBody(ia, A, A0);
  putBody(ib, B, B0);
  if (ia == PLAYER_ID || ib == PLAYER_ID) bumpShake = 1.0f;
}

void physics_resolveContacts(float dt) {
  if (segCount <= 0) return;
  for (int pass = 0; pass < 2; pass++) {
    for (int a = 0; a < NUM_RACERS; a++)
      for (int b = a + 1; b < NUM_RACERS; b++) pairContact(a, b, pass == 0 ? dt : 0.0f);
    // a push may have shoved somebody into a wall: same limits for all
    for (int i = 1; i < NUM_RACERS; i++) rivalWalls(i, dt);
    if (!crashed) playerWalls(dt, false);
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
  position = loopIncrease(position, dt * speed, trackLength);
}
