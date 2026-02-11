/*
  ═══════════════════════════════════════════════════════════════
  RENDERIZADO Y DIBUJO
  ═══════════════════════════════════════════════════════════════
*/

#ifndef RENDERING_H
#define RENDERING_H

#include <TFT_eSPI.h>
#include "config.h"
#include "structs.h"

// ═══════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES DE RENDERIZADO
// ═══════════════════════════════════════════════════════════════
extern TFT_eSPI tft;
extern TFT_eSprite spr;

extern RenderPt rCache[DRAW_DIST];
extern int16_t  rClip[DRAW_DIST];

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES DE DIBUJO
// ═══════════════════════════════════════════════════════════════

// Dibujar sprites (árboles, arbustos, etc.)
void drawSpriteShape(int type, int sx, int sy, float scale, int16_t clipY, int timeOfDay);

// Dibujar coche de tráfico
void drawTrafficCar(int cx, int cy, float scale, uint16_t col, int16_t clipY);

// Dibujar coche del jugador
void drawPlayerCar();

// Dibujar el cielo
void drawSky(float position, float playerZdist, int timeOfDay);

// Dibujar un trapecio 3D (quad)
void drawQuad(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, uint16_t c);

// Dibujar la carretera completa
void drawRoad(float position, float playerX, float playerZdist,
              float cameraDepth, int timeOfDay);

// Dibujar el HUD
void drawHUD(float speed, float maxSpeed, float currentLapTime, float bestLapTime);

// Dibujar velocímetro circular estilo Top Gear
void drawSpeedometer(float speed, float maxSpeed);

// Dibujar pantalla de inicio
void drawStartScreen();

// Dibujar mensaje de crash
void drawCrashMessage();

#endif // RENDERING_H
