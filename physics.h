/*
  ═══════════════════════════════════════════════════════════════
  GAME PHYSICS AND LOGIC
  ═══════════════════════════════════════════════════════════════
*/

#ifndef PHYSICS_H
#define PHYSICS_H

#include "input.h"

// ═══════════════════════════════════════════════════════════════
//  GLOBAL PHYSICS VARIABLES
// ═══════════════════════════════════════════════════════════════
extern float cameraDepth;
extern float playerZdist;
extern float position;
extern float playerX;
extern float speed;
extern float maxSpeed;
extern float centrifugal;
extern bool crashed;
extern unsigned long crashTimer;
extern unsigned long lastFrameMs;
extern float currentLapTime;
extern float lastLapTime;
extern float bestLapTime;
extern float prevPosition;
extern int currentLap;
extern int totalLaps;

extern float velocityX;        // Lateral velocity (for drift)
extern float acceleration;     // Current acceleration
extern float driftAngle;       // Drift angle (visual)
extern float steerVisual;      // Smoothed steering for the car's yaw (-1..1)
extern bool  braking;
extern bool  autopilot;        // true while the demo driver is in control
extern float offRoad;          // 0 on tarmac .. 1 on grass (visual shake)

// ═══════════════════════════════════════════════════════════════
//  PHYSICS FUNCTIONS
// ═══════════════════════════════════════════════════════════════
void initPhysics();
void handleInput(float dt, const InputState& in);
void updatePhysics(float dt);
void checkCollisions();

#endif // PHYSICS_H
