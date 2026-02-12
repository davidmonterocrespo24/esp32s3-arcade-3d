/*
  ═══════════════════════════════════════════════════════════════
  FÍSICA Y LÓGICA DEL JUEGO
  ═══════════════════════════════════════════════════════════════
*/

#ifndef PHYSICS_H
#define PHYSICS_H

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES DE FÍSICA
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

// Variables de física avanzada
extern float velocityX;        // Velocidad lateral (para derrape)
extern float acceleration;     // Aceleración actual
extern float driftAngle;       // Ángulo de derrape

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES DE FÍSICA
// ═══════════════════════════════════════════════════════════════

// Inicializar variables de física
void initPhysics();

// Manejar entrada del jugador (modo demo autopilot)
void handleInput(float dt);

// Actualizar física del juego
void updatePhysics(float dt);

// Verificar colisiones
void checkCollisions();

#endif // PHYSICS_H
