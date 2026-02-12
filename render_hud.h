/*
  ═══════════════════════════════════════════════════════════════
  RENDERIZADO DEL HUD Y VELOCÍMETRO
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_HUD_H
#define RENDER_HUD_H

#include <Arduino.h>

// Dibuja el HUD completo (contador de vueltas, tiempos, velocímetro)
void drawHUD(float speed, float maxSpeed, float currentLapTime, float bestLapTime);

// Dibuja el velocímetro circular
void drawSpeedometer(float speed, float maxSpeed);

#endif // RENDER_HUD_H
