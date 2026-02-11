/*
  ═══════════════════════════════════════════════════════════════
  GESTIÓN DE COLORES Y PALETAS
  ═══════════════════════════════════════════════════════════════
*/

#ifndef COLORS_H
#define COLORS_H

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
//  PALETA DE COLORES (Variables Globales)
// ═══════════════════════════════════════════════════════════════
extern uint16_t colSky1, colSky2, colSky3;
extern uint16_t colGrassL, colGrassD;
extern uint16_t colRoadL, colRoadD;
extern uint16_t colRumbleL, colRumbleD;
extern uint16_t colLane, colFog;

// ═══════════════════════════════════════════════════════════════
//  FUNCIONES
// ═══════════════════════════════════════════════════════════════

// Convertir RGB a formato RGB565
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);

// Oscurecer un color
uint16_t darkenCol(uint16_t c, float f);

// Interpolar entre dos colores
uint16_t lerpCol(uint16_t c1, uint16_t c2, float t);

// Inicializar colores según la hora del día
void initColors(int timeOfDay);

#endif // COLORS_H
