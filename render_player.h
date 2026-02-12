/*
  ═══════════════════════════════════════════════════════════════
  RENDERIZADO DEL AUTO DEL JUGADOR
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDER_PLAYER_H
#define RENDER_PLAYER_H

#include <Arduino.h>

// Dibuja el auto del jugador en 3D
void drawPlayerCar();

// Dibuja la pantalla de inicio con el auto rotando
void drawStartScreen(float time);

// Dibuja el mensaje de crash
void drawCrashMessage();

#endif // RENDER_PLAYER_H
