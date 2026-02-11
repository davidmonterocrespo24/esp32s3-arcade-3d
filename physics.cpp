/*
  ═══════════════════════════════════════════════════════════════
  IMPLEMENTACIÓN DE FÍSICA Y LÓGICA
  ═══════════════════════════════════════════════════════════════
*/

#include "physics.h"
#include "track.h"
#include "utils.h"
#include "config.h"
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES (Definición)
// ═══════════════════════════════════════════════════════════════
float cameraDepth;
float playerZdist;
float position   = 0;
float playerX    = 0;
float speed      = 0;
float maxSpeed;
float centrifugal = 0.3f;

bool  crashed     = false;
unsigned long crashTimer = 0;
unsigned long lastFrameMs;

float currentLapTime = 0;
float lastLapTime    = 0;
float bestLapTime    = 0;
float prevPosition   = 0;

// ═══════════════════════════════════════════════════════════════
//  IMPLEMENTACIÓN
// ═══════════════════════════════════════════════════════════════

void initPhysics() {
  float fovRad = FOV_DEG * PI / 180.0;
  cameraDepth  = 1.0 / tanf(fovRad / 2.0);
  playerZdist  = CAM_HEIGHT * cameraDepth;
  maxSpeed     = SEG_LEN * 30.0;

  position     = 0;
  playerX      = 0;
  speed        = 0;
  crashed      = false;
  currentLapTime = 0;
  lastLapTime    = 0;
  bestLapTime    = 0;
  prevPosition   = 0;
}

void handleInput(float dt) {
  // === MODO DEMO: Auto-piloto ===
  speed += (maxSpeed / 3.0) * dt;
  if (speed > maxSpeed * 0.85) speed = maxSpeed * 0.85;

  // Leer la curva actual para anticipar el giro
  int pSeg = findSegIdx(position + playerZdist);
  float curCurve = segments[pSeg].curve;

  // Contragirar según la curva + volver al centro
  float target = -curCurve * 0.12;
  float steer = (target - playerX) * 3.0 * dt;
  playerX += steer;

  playerX = clampF(playerX, -0.8, 0.8);
}

void updatePhysics(float dt) {
  int pSeg = findSegIdx(position + playerZdist);
  float spPct = speed / maxSpeed;
  float steerDx = dt * 2.0 * spPct;
  playerX -= steerDx * spPct * segments[pSeg].curve * centrifugal;

  prevPosition = position;
  position = loopIncrease(position, dt * speed, trackLength);

  // Detectar completar vuelta
  if (position < prevPosition && prevPosition > trackLength * 0.9) {
    if (currentLapTime > 5.0) {
      lastLapTime = currentLapTime;
      if (bestLapTime <= 0 || currentLapTime < bestLapTime)
        bestLapTime = currentLapTime;
    }
    currentLapTime = 0;
  }
  currentLapTime += dt;

  // Actualizar tráfico
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

  // Colisiones con tráfico
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

  // Colisiones con sprites
  if (playerX < -1.0 || playerX > 1.0) {
    Segment& s = segments[pSeg];
    if (s.spriteType >= 0 && overlapChk(playerX, playerW, s.spriteOffset, 0.4)) {
      speed *= 0.2;
      if (speed > maxSpeed * 0.25) {
        crashed = true;
        crashTimer = millis();
      }
    }
  }

  // Salirse completamente de la carretera
  if (playerX <= -2.4 || playerX >= 2.4) {
    speed *= 0.5;
    if (speed > maxSpeed * 0.4) {
      crashed = true;
      crashTimer = millis();
    }
  }
}
