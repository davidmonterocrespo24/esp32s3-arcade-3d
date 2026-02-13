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
float centrifugal = 0.18f;

bool  crashed     = false;
unsigned long crashTimer = 0;
unsigned long lastFrameMs;

float currentLapTime = 0;
float lastLapTime    = 0;
float bestLapTime    = 0;
float prevPosition   = 0;
int currentLap       = 1;
int totalLaps        = 3;

// Variables de física avanzada
float velocityX      = 0;     // Velocidad lateral (para derrape)
float acceleration   = 0;     // Aceleración actual
float driftAngle     = 0;     // Ángulo de derrape

// ═══════════════════════════════════════════════════════════════
//  IMPLEMENTACIÓN
// ═══════════════════════════════════════════════════════════════

void initPhysics() {
  float fovRad = FOV_DEG * PI / 180.0;
  cameraDepth  = 1.0 / tanf(fovRad / 2.0);
  playerZdist  = CAM_HEIGHT * cameraDepth;
  maxSpeed     = SEG_LEN * 65.0;  // ~246 km/h (68 m/s) a escala real

  position     = 0;
  playerX      = 0;
  speed        = 0;
  crashed      = false;
  currentLapTime = 0;
  lastLapTime    = 0;
  bestLapTime    = 0;
  prevPosition   = 0;

  // Inicializar física avanzada
  velocityX    = 0;
  acceleration = 0;
  driftAngle   = 0;
}

void handleInput(float dt) {
  // === MODO DEMO: Auto-piloto con aceleración progresiva ===

  // Aceleración progresiva (no instantánea)
  float targetAccel = maxSpeed * 0.9f;  // Aceleración objetivo (0→max en ~2.2s)
  float accelSpeed = 180.0f;            // Subida gradual del motor

  if (speed < maxSpeed * 0.90f) {
    // Aplicar aceleración gradualmente
    acceleration += accelSpeed * dt;
    if (acceleration > targetAccel) acceleration = targetAccel;
  } else {
    // Cerca de velocidad máxima, reducir aceleración
    acceleration *= 0.97f;
  }

  // Leer la curva actual para anticipar el giro
  int pSeg = findSegIdx(position + playerZdist);
  float curCurve = segments[pSeg].curve;

  // Contragirar según la curva + volver al centro
  float target = -curCurve * 0.12;
  float steer = (target - playerX) * 1.8 * dt;
  playerX += steer;

  playerX = clampF(playerX, -0.8, 0.8);
}

void updatePhysics(float dt) {
  int pSeg = findSegIdx(position + playerZdist);
  int prevSegIdx = (pSeg - 1 + TOTAL_SEGS) % TOTAL_SEGS;

  float spPct = speed / maxSpeed;

  // === GRAVEDAD: Efecto de pendientes (subidas/bajadas) ===
  float currentY = segments[pSeg].y;
  float prevY = segments[prevSegIdx].y;
  float slope = (currentY - prevY) / SEG_LEN;  // Pendiente del terreno

  // Subiendo = más lento, bajando = más rápido
  float gravityEffect = -slope * 1600.0f;  // Factor de gravedad (proporcional a nueva velocidad)
  acceleration += gravityEffect * dt;

  // === ACELERACIÓN CON FRICCIÓN ===
  // Fricción del aire y del suelo
  float friction = 0.996f;  // Frenado ~3.3s desde velocidad máxima
  speed *= friction;

  // Aplicar aceleración a la velocidad
  speed += acceleration * dt;

  // Limitar velocidad
  if (speed > maxSpeed) speed = maxSpeed;
  if (speed < 0) speed = 0;

  // === DERRAPE: Física lateral en curvas ===
  float curveForce = segments[pSeg].curve * centrifugal * spPct;

  // Velocidad lateral aumenta con la fuerza de la curva
  velocityX += curveForce * dt * 3.0f;

  // Fricción lateral (el auto intenta volver al centro)
  velocityX *= 0.90f;

  // Aplicar velocidad lateral a la posición
  playerX -= velocityX * dt;

  // También aplicar el empuje de la curva (centrífugo)
  float steerDx = dt * 1.5 * spPct;
  playerX -= steerDx * spPct * segments[pSeg].curve * centrifugal;

  // === ÁNGULO DE DERRAPE VISUAL ===
  // Calcular ángulo basado en velocidad lateral y velocidad forward
  if (speed > 0.1f) {
    driftAngle = atan2f(velocityX * 10.0f, speed / maxSpeed) * 0.5f;
    driftAngle = clampF(driftAngle, -0.5f, 0.5f);
  } else {
    driftAngle *= 0.9f;  // Reducir ángulo gradualmente
  }

  prevPosition = position;
  position = loopIncrease(position, dt * speed, trackLength);

  // Detectar completar vuelta
  if (position < prevPosition && prevPosition > trackLength * 0.9) {
    if (currentLapTime > 5.0) {
      lastLapTime = currentLapTime;
      if (bestLapTime <= 0 || currentLapTime < bestLapTime)
        bestLapTime = currentLapTime;

      // Avanzar a la siguiente vuelta
      if (currentLap < totalLaps) {
        currentLap++;
      } else {
        // ¡Carrera terminada! Reiniciar
        currentLap = 1;
      }
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
  Segment& s = segments[pSeg];

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

  // Colisiones con paredes del tunel
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

  // Colisiones con sprites
  if (playerX < -1.0 || playerX > 1.0) {
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
