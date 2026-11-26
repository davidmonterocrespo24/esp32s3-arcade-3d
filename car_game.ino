/*
  ═══════════════════════════════════════════════════════════════
  ESP32-S3 Pseudo-3D Racing Game — ARCADE 3D

  The sketch is thin on purpose: game.cpp owns the state machine
  (title, menus, Grand Prix / quick race, countdown, race, results).
  See wiki/10-game-architecture.md for the module map.

  Memory map (ESP32-S3, 8 MB octal PSRAM):
    internal SRAM : framebuffer 150 KB, car texture 32 KB, 2x5 KB DMA
                    bands, mesh work arrays ~17 KB
    PSRAM         : 2 x 150 KB scanout snapshots, track segments 48 KB
    flash         : mesh, texture source, palettes, font
  ═══════════════════════════════════════════════════════════════
*/

#include <Arduino.h>
#include "game.h"

void setup() {
  game_setup();
}

void loop() {
  game_loop();
}
