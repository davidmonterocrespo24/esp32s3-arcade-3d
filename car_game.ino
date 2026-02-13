/*
  ═══════════════════════════════════════════════════════════════
  ESP32 Pseudo-3D Racing Game — TFT_eSPI (Double Buffered)

  Modules:
  - config.h       : Constants and hardware definitions
  - structs.h      : Data structures (Segment, RenderPt, TrafficCar, CompetitorCar)
  - colors.cpp/h   : Color management and palettes
  - utils.cpp/h    : Math utility functions
  - track.cpp/h    : Track generation and competitors
  - rendering.cpp/h: Drawing and rendering functions
  - physics.cpp/h  : Game physics, collisions, and AI
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
#include "render_player.h"
#include "render_hud.h"

// ═══════════════════════════════════════════════════════════════
//  TIME OF DAY CONTROL
// ═══════════════════════════════════════════════════════════════
int  timeOfDay = 0;            // 0=day, 1=sunset, 2=night
long distSinceTimeChange = 0;

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  // Configure button pins
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  randomSeed(analogRead(0));

  // Configure TFT backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Initialize TFT display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Create main sprite for double buffering (uses PSRAM)
  spr.setColorDepth(16);
  spr.setAttribute(PSRAM_ENABLE, true);
  if (spr.createSprite(SCR_W, SCR_H) == nullptr) {
    Serial.println("ERROR: Failed to create main sprite!");
  }

  // Initialize physics (also sets gameState = STATE_COUNTDOWN)
  initPhysics();

  // Debug PSRAM
  Serial.print("Total PSRAM: "); Serial.println(ESP.getPsramSize());
  Serial.print("Free PSRAM:  "); Serial.println(ESP.getFreePsram());

  // Initialize colors
  initColors(timeOfDay);

  // Generate parallax mountains in PSRAM
  initBackground();

  // Build track
  buildTrack();

  // Initialize race competitors at starting grid
  initCompetitors(maxSpeed);

  // Show start screen with rotating car (3 seconds)
  unsigned long startTime = millis();
  while (millis() - startTime < 3000) {
    float animTime = (millis() - startTime) * 0.001f;
    drawStartScreen(animTime);
    delay(16);
  }

  // Begin countdown immediately after start screen
  countdownStart = millis();
  gameState = STATE_COUNTDOWN;

  lastFrameMs = millis();
  distSinceTimeChange = 0;
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameMs) / 1000.0f;
  lastFrameMs = now;
  if (dt > 0.1f) dt = 0.1f;

  // ── STATE: COUNTDOWN ──────────────────────────────────────────
  if (gameState == STATE_COUNTDOWN) {
    unsigned long elapsed = millis() - countdownStart;
    if (elapsed >= 3600) {
      // GO! — transition to racing
      gameState = STATE_RACING;
    }
    // During countdown: render the scene frozen at starting line
    // (no physics update, cars stay still)
    drawSky(position, playerZdist, timeOfDay, skyOffset);
    drawRoad(position, playerX, playerZdist, cameraDepth, timeOfDay);
    drawPlayerCar();
    drawCountdown();
    spr.pushSprite(0, 0);
    return;
  }

  // ── STATE: FINISHED ───────────────────────────────────────────
  if (gameState == STATE_FINISHED) {
    if (!raceResultShown) {
      raceResultShown = true;
    }
    drawSky(position, playerZdist, timeOfDay, skyOffset);
    drawRoad(position, playerX, playerZdist, cameraDepth, timeOfDay);
    drawPlayerCar();
    drawHUD(speed, maxSpeed, currentLapTime, bestLapTime);
    drawRaceResults();
    spr.pushSprite(0, 0);

    // Auto-restart after 3 seconds
    static unsigned long finishTime = 0;
    if (finishTime == 0) finishTime = millis();
    if (millis() - finishTime > 3000) {
      finishTime = 0;
      // Full reset
      initPhysics();
      buildTrack();
      initCompetitors(maxSpeed);
      countdownStart = millis();
      gameState = STATE_COUNTDOWN;
    }
    return;
  }

  // ── STATE: RACING ─────────────────────────────────────────────
  if (!crashed) {
    handleInput(dt);
    updatePhysics(dt);
    updateCompetitors(dt);
    checkCollisions();

    // Parallax sky scroll
    int pSeg = findSegIdx(position + playerZdist);
    float curveForce = segments[pSeg].curve;
    skyOffset += curveForce * (speed / maxSpeed) * 150.0f * dt;

    // Check if player finished all laps
    if (currentLap > totalLaps) {
      gameState = STATE_FINISHED;
    }
  }

  // Render frame
  drawSky(position, playerZdist, timeOfDay, skyOffset);
  drawRoad(position, playerX, playerZdist, cameraDepth, timeOfDay);
  drawPlayerCar();
  drawHUD(speed, maxSpeed, currentLapTime, bestLapTime);

  // Crash overlay
  if (crashed) {
    drawCrashMessage();
    if (millis() - crashTimer > 2000) {
      crashed = false;
      speed   = 0;
      playerX = 0;
    }
  }

  spr.pushSprite(0, 0);

  // Time of day progression
  distSinceTimeChange += (int)(speed * dt);
  if (distSinceTimeChange > 180000) {
    distSinceTimeChange = 0;
    timeOfDay = (timeOfDay + 1) % 3;
    initColors(timeOfDay);
  }
}
