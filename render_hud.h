/*
  ═══════════════════════════════════════════════════════════════
  HUD AND SPEEDOMETER RENDERING
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_HUD_H
#define RENDER_HUD_H

#include <Arduino.h>

// Draws the complete HUD (lap counter, times, speedometer)
void drawHUD(float speed, float maxSpeed, float currentLapTime, float bestLapTime);

// Draws the circular speedometer
void drawSpeedometer(float speed, float maxSpeed);

// Draws the 3-2-1-GO countdown overlay
void drawCountdown();

// Draws the race finish results screen
void drawRaceResults();

#endif // RENDER_HUD_H
