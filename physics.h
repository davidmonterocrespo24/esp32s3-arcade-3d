/*
  ═══════════════════════════════════════════════════════════════
  PLAYER CAR PHYSICS
  position is the CAMERA's track position; the player's car is at
  position + playerZdist. Also owns: rival bumps, solid limits (rails,
  tunnel walls, building faces), solid scenery, off-road materials,
  turbo, the autopilot (demo / after the finish) and crash / reset.
  ═══════════════════════════════════════════════════════════════
*/

#ifndef PHYSICS_H
#define PHYSICS_H

#include "input.h"

// ═══════════════════════════════════════════════════════════════
//  TUNABLES (per-frame factors are defined at 60 Hz and rescaled by
//  dt, so the handling does not depend on the frame rate)
// ═══════════════════════════════════════════════════════════════
#define SPEED_MULTIPLIER   65.0f   // maxSpeed = SEG_LEN * SPEED_MULTIPLIER (~246 km/h)
#define ACCEL_TARGET       0.9f    // targetAccel = maxSpeed * ACCEL_TARGET
#define ACCEL_RAMP         420.0f  // how fast acceleration ramps up (u/s^3)
#define ACCEL_NEAR_MAX     0.90f   // speed threshold to reduce acceleration
#define ACCEL_DAMPING      0.97f   // multiplier when approaching maxSpeed (per 60 Hz frame)
#define FRICTION           0.996f  // friction per 60 Hz frame (~3.3 s coast-down)
#define BRAKE_DECEL        9000.0f // brake deceleration (u/s^2)
#define GRAVITY_FACTOR     1600.0f // effect of slopes on speed
#define CENTRIFUGAL        0.18f   // centrifugal force in curves
#define CURVE_FORCE        3.0f    // lateral force multiplier in curves
#define LATERAL_FRICTION   0.90f   // lateral velocity damping per 60 Hz frame
#define STEER_AUTO         1.8f    // autopilot response
#define STEER_MANUAL       2.2f    // manual steering speed (road half-widths / s)
#define CENTRIFUGAL_DX     1.5f    // lateral centrifugal push factor
#define TURBO_BOOST        0.12f   // +12 % top speed while boosting
#define TURBO_ACCEL        1.6f    // acceleration multiplier while boosting
#define TURBO_DRAIN        (1.0f / 3.0f)    // a full meter lasts 3 s
#define TURBO_REFILL       (1.0f / 20.0f)   // and refills in ~20 s
#define CRASH_TIME         2.0f    // seconds of spin before the reset

// ---- additions (feature phase) ----
#define BRAKE_ACCEL_KEEP   0.985f  // throttle state kept per 60 Hz frame while braking. It
                                   // used to drop to 0, and with the slow ACCEL_RAMP a single
                                   // brake tap at speed cost ~5 s of sagging speed.
#define ROCKET_WINDOW      0.30f   // TURBO pressed within this long after GO...
#define ROCKET_ACCEL       0.0f    // ...starts with at least this throttle state (u/s^2)...
#define ROCKET_TIME        0.45f   // ...and a free boost: ~+110 u/s^2 of throttle, worth
                                   // ~2.5 car lengths by top speed (1 s = ~10 car lengths)
#define BUMP_REST          0.30f   // restitution of nose-to-tail bumps
#define BUMP_CRASH         0.50f   // closing speed (fraction of maxSpeed) that crashes the player
#define SIDE_KICK          0.35f   // lateral bounce of a side contact (half-widths / s)
#define SIDE_RUB_KEEP      0.996f  // speed kept per 60 Hz frame of side contact (~1 % a bump)
#define DRAFT_BOOST        0.035f  // slipstream: top speed +3.5 % right behind a car...
#define DRAFT_RANGE        2750.0f // ...fading to 0 at this bumper gap (2.5 car lengths)
#define DRAFT_WIDTH        0.30f   // |dx| below this is "behind" it

// ═══════════════════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════════════════
extern float cameraDepth;
extern float playerZdist;      // camera -> player car distance
extern float position;         // camera track position [0, trackLength)
extern float playerX;          // lateral, road half-widths
extern float speed;
extern float maxSpeed;
extern float centrifugal;
extern bool  crashed;
extern float crashTime;        // seconds since the crash (while crashed)

extern float velocityX;        // lateral velocity (drift)
extern float acceleration;     // current throttle acceleration
extern float driftAngle;       // drift angle (visual)
extern float steerVisual;      // smoothed steering for the car's yaw (-1..1)
extern bool  braking;
extern bool  boosting;         // turbo active this frame
extern float turboMeter;       // 0..1
extern bool  autopilot;        // the AI drives the player car (demo, after the finish)
extern float offRoad;          // 0 on tarmac .. 1 on rough ground (visual shake)
extern float rocketTime;       // > 0 while a rocket start boost lasts (seconds left)
extern bool  rocketStarted;    // the player got a rocket start in this race (HUD banner)
extern float bumpShake;        // 0..1, set by a contact, decays (camera / car jolt)
extern float draft;            // 0..1, the player's slipstream (HUD can show it)

// ═══════════════════════════════════════════════════════════════
//  FUNCTIONS
// ═══════════════════════════════════════════════════════════════
void initPhysics();                              // constants + a car at rest at 0
void physics_placePlayer(float camPos, float x); // put the car at rest (grid, reset)
void handleInput(float dt, const InputState& in);// steering, brake, turbo, autopilot
void updatePhysics(float dt);                    // speed, gravity, drift, materials
void checkCollisions(float dt);                  // walls, rails, scenery (rivals: see below)
void updateCrash(float dt);                      // while crashed: spin, then reset

// ---- additions (feature phase) ----
// Car-to-car contacts for every pair (player and rivals), with everybody
// at the same time step: rivals_update() calls it after moving the rivals,
// so checkCollisions() only handles walls and scenery for the player.
void  physics_resolveContacts(float dt);
// Largest |x| a car centre may have on one side (0 left, 1 right) at track
// position z: the nearest solid limit under the car's footprint (rails,
// tunnel walls, building faces, OFFROAD_MAX_X) minus CAR_HALF_W.
float physics_maxX(float z, int side);
// Instant slipstream of racer i (0..1) from the car right in front of it
// in the same lane; cars smooth it before use.
float physics_draftOf(int i);
// Throttle state (u/s^2) that holds the cruise speed (ACCEL_NEAR_MAX * top)
// against friction: the engine eases back to this, never below, at the top.
float physics_cruiseAccel(float top);

#endif // PHYSICS_H
