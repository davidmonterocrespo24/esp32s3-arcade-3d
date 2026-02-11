/*
  ═══════════════════════════════════════════════════════════════
  ESP32 Pseudo-3D Racing Game — TFT_eSPI (Double Buffered)

  ESTRUCTURA MODULAR:
  - config.h       : Constantes y definiciones del hardware
  - structs.h      : Estructuras de datos (Segment, RenderPt, TrafficCar)
  - colors.cpp/h   : Gestión de colores y paletas
  - utils.cpp/h    : Funciones utilitarias matemáticas
  - track.cpp/h    : Generación de pista y tráfico
  - rendering.cpp/h: Funciones de dibujo y renderizado
  - physics.cpp/h  : Física del juego y colisiones
  ═══════════════════════════════════════════════════════════════
*/

#include <SPI.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "structs.h"
#include "colors.h"
#include "utils.h"
#include "track.h"
#include "rendering.h"
#include "physics.h"

// ═══════════════════════════════════════════════════════════════
//  VARIABLES DE CONTROL DE TIEMPO Y DÍA/NOCHE
// ═══════════════════════════════════════════════════════════════
int  timeOfDay = 0;            // 0=día, 1=atardecer, 2=noche
long distSinceTimeChange = 0;

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // Configurar pines de botones
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  randomSeed(analogRead(0));

  // Configurar backlight de la pantalla
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Inicializar pantalla TFT
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Crear sprite para double buffering en PSRAM (ahorra ~150KB RAM)
  spr.setColorDepth(16);
  spr.createSprite(SCR_W, SCR_H);
  spr.setAttribute(PSRAM_ENABLE, true);

  // Inicializar física
  initPhysics();

  // Inicializar colores
  initColors(timeOfDay);

  // ¡NUEVO! Generar montañas parallax en PSRAM
  initBackground();

  // Construir pista
  buildTrack();

  // Inicializar tráfico
  initTraffic(maxSpeed);

  // Mostrar pantalla de inicio
  drawStartScreen();
  delay(2500);

  lastFrameMs = millis();
  distSinceTimeChange = 0;
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameMs) / 1000.0;
  lastFrameMs = now;
  if (dt > 0.1) dt = 0.1; // Limitar delta time

  // Actualizar juego si no está crashed
  if (!crashed) {
    handleInput(dt);
    updatePhysics(dt);
    checkCollisions();

    // --- MAGIA DEL PARALLAX ---
    // El fondo se mueve según la curva y velocidad (efecto Horizon Chase)
    int pSeg = findSegIdx(position + playerZdist);
    float curveForce = segments[pSeg].curve;
    // Factor 150.0 controla velocidad de rotación del fondo
    skyOffset += curveForce * (speed / maxSpeed) * 150.0f * dt;
  }

  // Renderizar frame en el sprite (buffer)
  drawSky(position, playerZdist, timeOfDay, skyOffset);
  drawRoad(position, playerX, playerZdist, cameraDepth, timeOfDay);
  drawPlayerCar();
  drawHUD(speed, maxSpeed, currentLapTime, bestLapTime);

  // Mostrar mensaje de crash
  if (crashed) {
    drawCrashMessage();
    if (millis() - crashTimer > 2000) {
      crashed = false;
      speed   = 0;
      playerX = 0;
    }
  }

  // Enviar el frame completo a la pantalla (double buffering)
  spr.pushSprite(0, 0);

  // Cambiar hora del día según distancia recorrida
  distSinceTimeChange += (int)(speed * dt);
  if (distSinceTimeChange > 180000) {
    distSinceTimeChange = 0;
    timeOfDay = (timeOfDay + 1) % 3;
    initColors(timeOfDay);
  }
}
