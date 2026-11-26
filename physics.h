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

// ═══════════════════════════════════════════════════════════════
//  FUNCTIONS
// ═══════════════════════════════════════════════════════════════
void initPhysics();                              // constants + a car at rest at 0
void physics_placePlayer(float camPos, float x); // put the car at rest (grid, reset)
void handleInput(float dt, const InputState& in);// steering, brake, turbo, autopilot
void updatePhysics(float dt);                    // speed, gravity, drift, materials
void checkCollisions(float dt);                  // rivals, walls, rails, scenery
void updateCrash(float dt);                      // while crashed: spin, then reset

#endif // PHYSICS_H
