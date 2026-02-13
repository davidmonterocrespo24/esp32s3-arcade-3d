/*
  ═══════════════════════════════════════════════════════════════
  GAME PHYSICS AND LOGIC
  ═══════════════════════════════════════════════════════════════
*/

#ifndef PHYSICS_H
#define PHYSICS_H

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

// Advanced physics variables
extern float velocityX;        // Lateral velocity (for drift)
extern float acceleration;     // Current acceleration
extern float driftAngle;       // Drift angle

// ═══════════════════════════════════════════════════════════════
//  PHYSICS FUNCTIONS
// ═══════════════════════════════════════════════════════════════

// Initialize physics variables
void initPhysics();

// Handle player input (demo autopilot mode)
void handleInput(float dt);

// Update game physics
void updatePhysics(float dt);

// Check collisions
void checkCollisions();

#endif // PHYSICS_H
