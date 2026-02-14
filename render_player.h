/*
  ═══════════════════════════════════════════════════════════════
  PLAYER CAR RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_PLAYER_H
#define RENDER_PLAYER_H

#include <Arduino.h>

// Draws the player car in 3D
void drawPlayerCar();

// Draws the start screen with the rotating car
void drawStartScreen(float time);

// Draws the crash message
void drawCrashMessage();

// Draws a competitor car at screen position with perspective projection
void drawCompetitorCar(int screenX, int screenY, float camZ, uint16_t color, float driftAngle);

#endif // RENDER_PLAYER_H
