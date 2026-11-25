/*
  ═══════════════════════════════════════════════════════════════
  ESP32-S3 Pseudo-3D Racing Game

  Modules:
  - config.h / board.h  : game constants / pin maps per board
  - gfx.cpp             : software framebuffer + rasterizer + font
  - display_esp32.cpp   : SPI DMA scanout on core 0 (ports' pipeline)
  - input_esp32.cpp     : stick / buttons
  - colors.cpp          : day / sunset / night / dawn palettes
  - track.cpp           : track, tunnels, city, scenery, traffic
  - physics.cpp         : speed, drift, collisions, laps
  - rendering.cpp       : sky and parallax background
  - render_road.cpp     : road, tunnel, buildings, sprites, traffic
  - render_player.cpp   : textured 3D car renderer
  - render_hud.cpp      : HUD

  Memory map (ESP32-S3, 8 MB octal PSRAM):
    internal SRAM : framebuffer 150 KB, car texture 32 KB, 2x5 KB DMA
                    bands, track 16 KB, mesh work arrays ~13 KB
    PSRAM         : 2 x 150 KB scanout snapshots
    flash         : mesh, texture source, palettes, font
  ═══════════════════════════════════════════════════════════════
*/

#include <Arduino.h>

#include "config.h"
#include "platform.h"
#include "gfx.h"
#include "display.h"
#include "input.h"
#include "colors.h"
#include "utils.h"
#include "track.h"
#include "rendering.h"
#include "physics.h"

enum GameState { STATE_TITLE, STATE_RACE };
static GameState gameState = STATE_TITLE;
static unsigned long stateStartMs = 0;

// Frame statistics for the HUD
static float fpsSmooth = 0, lcdFpsSmooth = 0, frameMsSmooth = 0;
static uint32_t lastShown = 0;
static unsigned long lastStatMs = 0;
static int framesSinceStat = 0;

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(50);
  plat_report_memory("boot");

  input_init();
  // DMA bands + PSRAM snapshots first, then the big internal framebuffer
  // while the internal heap is still unfragmented.
  if (!display_init()) plat_log("display_init FAILED\n");
  if (!gfx_init()) plat_log("gfx_init FAILED: no memory for the framebuffer\n");
  initCarRenderer();
  plat_report_memory("ready");

  randomSeed(micros());
  initPhysics();
  setDayCycle(CYCLE_START);
  initBackground();
  buildTrack();
  initTraffic(maxSpeed);

  lastFrameMs = millis();
  stateStartMs = lastFrameMs;
  lastStatMs = lastFrameMs;
  gameState = STATE_TITLE;
}

static void updateStats(unsigned long now, float frameMs) {
  framesSinceStat++;
  frameMsSmooth += (frameMs - frameMsSmooth) * 0.1f;
  if (now - lastStatMs >= 500) {
    float secs = (now - lastStatMs) / 1000.0f;
    fpsSmooth = framesSinceStat / secs;
    uint32_t shown = displayStats.shown;
    lcdFpsSmooth = (shown - lastShown) / secs;
    lastShown = shown;
    framesSinceStat = 0;
    lastStatMs = now;
  }
}

// ═══════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();
  unsigned long t0us = micros();
  float dt = (now - lastFrameMs) / 1000.0f;
  lastFrameMs = now;
  if (dt > 0.1f) dt = 0.1f;
  float timeSec = now / 1000.0f;

  InputState in = input_read();

  // ── TITLE: rotating car until a control is touched (or 6 s) ──
  if (gameState == STATE_TITLE) {
    drawStartScreen(timeSec);
    display_present(gfx_fb);
    if ((in.any && now - stateStartMs > 800) || now - stateStartMs > 6000) {
      gameState = STATE_RACE;
      stateStartMs = now;
      // START alone keeps the demo driver until the player steers or brakes
      autopilot = (in.steer == 0 && !in.brake);
    }
    updateStats(now, (micros() - t0us) / 1000.0f);
    return;
  }

  // ── RACE ──
  if (!crashed) {
    handleInput(dt, in);
    updatePhysics(dt);
    checkCollisions();

    // Parallax: the background turns with the curves
    int pSeg = findSegIdx(position + playerZdist);
    skyOffset += segments[pSeg].curve * (speed / maxSpeed) * 150.0f * dt;

    // Day -> sunset -> night -> dawn, driven by distance
    advanceDayCycle(speed * dt);
  }

  drawSky(skyOffset, timeSec);
  drawRoad(position, playerX, playerZdist, cameraDepth);
  drawPlayerCar(timeSec);
  drawHUD(timeSec, fpsSmooth, lcdFpsSmooth, frameMsSmooth);

  if (crashed) {
    drawCrashMessage((now - crashTimer) / 1000.0f);
    if (now - crashTimer > 2000) {
      crashed   = false;
      speed     = 0;
      playerX   = 0;
      velocityX = 0;
    }
  }

  // Hand the frame to the scanout. On the ESP32 this copies it to a PSRAM
  // snapshot (or drops it if the panel is still busy) and returns at once.
  display_present(gfx_fb);
  updateStats(now, (micros() - t0us) / 1000.0f);
}
